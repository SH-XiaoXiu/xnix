/**
 * @file term/main.c
 * @brief 终端服务
 *
 * 组合 chardev 输入/输出设备为双向终端流.
 * shell 通过 IO_READ/IO_WRITE 与 term 通信,
 * term 通过 CHARDEV_READ/WRITE 与底层设备通信.
 *
 * 架构 (每个终端实例):
 *   input_thread: CHARDEV_READ(input_ep) → 行规程 → input_queue
 *   service_thread: IO_READ/IO_WRITE(term_ep) ↔ input_queue / CHARDEV_WRITE(output_ep)
 */

#include <xnix/abi/io.h>
#include <xnix/abi/input.h>
#include <xnix/abi/tty.h>
#include <xnix/protocol/chardev.h>
#include <xnix/protocol/devfs.h>
#include <xnix/protocol/displaydev.h>
#include <xnix/protocol/input.h>
#include <xnix/protocol/inputdev.h>
#include <xnix/protocol/tty.h>

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* ============== 终端实例 ============== */

#define TERM_INPUT_BUF_SIZE 256
#define TERM_LINE_BUF_SIZE  256
#define SERIAL_TERM_COUNT   4

enum ldisc_mode {
    LDISC_RAW    = 0,
    LDISC_COOKED = 1,
};

enum dev_proto {
    DEV_CHARDEV    = 0, /* CHARDEV_READ/WRITE (串口) */
    DEV_INPUTDEV   = 1, /* INPUTDEV_READ (键盘) */
    DEV_DISPLAYDEV = 2, /* DISPDEV_WRITE (显示) */
};

enum ansi_state {
    ANSI_STATE_NORMAL = 0,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI,
};

struct ansi_parser {
    enum ansi_state state;
    char            param_buf[16];
    int             param_pos;
};

struct term_cell {
    char    ch;
    uint8_t fg;
    uint8_t bg;
};

struct terminal {
    int      id;
    handle_t term_ep;      /* shell 连接的 endpoint */
    handle_t input_ep;     /* 输入设备 */
    handle_t output_ep;    /* 输出设备 */
    enum dev_proto input_proto;
    enum dev_proto output_proto;

    /* 输入队列 */
    char              input_buf[TERM_INPUT_BUF_SIZE];
    volatile uint8_t  input_head;
    volatile uint8_t  input_tail;
    pthread_mutex_t   input_lock;

    /* 延迟回复 */
    int      pending_read;
    uint32_t pending_tid;
    uint32_t pending_max_len;

    /* 行规程 */
    enum ldisc_mode mode;
    int             echo;
    char            line_buf[TERM_LINE_BUF_SIZE];
    uint16_t        line_pos;

    /* 前台进程 */
    int foreground_pid;

    /* ANSI 输出解析 (VGA/displaydev 路径) */
    struct ansi_parser ansi;

    /* 显示 VT 状态 */
    int              is_display_vt;
    int              screen_rows;
    int              screen_cols;
    int              cursor_row;
    int              cursor_col;
    uint8_t          cur_fg;
    uint8_t          cur_bg;
    struct term_cell *screen;
};

#define MAX_TERMINALS ABI_TTY_MAX
static struct terminal g_terms[MAX_TERMINALS];
static int             g_term_count = 0;
static pthread_mutex_t g_display_lock;
static int             g_active_display_term = -1;

static void term_init(struct terminal *t, int id, handle_t term_ep,
                      handle_t input_ep, enum dev_proto input_proto,
                      handle_t output_ep, enum dev_proto output_proto);

static int register_tty_device(handle_t devfs_ep, int tty_id, handle_t tty_ep) {
    struct ipc_message reg = {0};
    struct ipc_message reply = {0};
    char name[8];

    snprintf(name, sizeof(name), "tty%d", tty_id);
    reg.regs.data[0] = UDM_DEVFS_REGISTER_TTY;
    reg.regs.data[1] = (uint32_t)tty_id;
    reg.handles.handles[0] = tty_ep;
    reg.handles.count = 1;
    reg.buffer.data = (uint64_t)(uintptr_t)name;
    reg.buffer.size = strlen(name);

    int ret = sys_ipc_call(devfs_ep, &reg, &reply, 1000);
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[term]",
                  " register /dev/tty%d failed: ipc=%d\n", tty_id, ret);
        return -1;
    }
    ret = (int32_t)reply.regs.data[1];
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[term]",
                  " register /dev/tty%d failed: devfs=%d\n", tty_id, ret);
    }
    return ret;
}

static struct terminal *find_term_by_id(int tty_id) {
    for (int i = 0; i < g_term_count; i++) {
        if (g_terms[i].id == tty_id)
            return &g_terms[i];
    }
    return NULL;
}

/* ============== 输入队列 ============== */

static int input_available(struct terminal *t) {
    if (t->input_head >= t->input_tail)
        return t->input_head - t->input_tail;
    return TERM_INPUT_BUF_SIZE - t->input_tail + t->input_head;
}

static void input_put(struct terminal *t, char c) {
    uint8_t next = (t->input_head + 1) % TERM_INPUT_BUF_SIZE;
    if (next == t->input_tail) return;
    t->input_buf[t->input_head] = c;
    t->input_head = next;
}

static int input_get(struct terminal *t) {
    if (t->input_head == t->input_tail) return -1;
    char c = t->input_buf[t->input_tail];
    t->input_tail = (t->input_tail + 1) % TERM_INPUT_BUF_SIZE;
    return (unsigned char)c;
}

/* ============== CHARDEV 客户端辅助 ============== */

static int chardev_write(handle_t ep, const void *buf, size_t len) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = CHARDEV_WRITE;
    msg.regs.data[1] = (uint32_t)len;
    msg.buffer.data  = (uint64_t)(uintptr_t)buf;
    msg.buffer.size  = (uint32_t)len;

    int ret = sys_ipc_call(ep, &msg, &reply, 5000);
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int chardev_read(handle_t ep, void *buf, size_t max) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = CHARDEV_READ;
    msg.regs.data[1] = (uint32_t)max;

    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = (uint32_t)max;

    int ret = sys_ipc_call(ep, &msg, &reply, 0); /* 无超时,阻塞 */
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int inputdev_read_event(handle_t ep, struct input_event *ev) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = INPUTDEV_READ;

    int ret = sys_ipc_call(ep, &msg, &reply, 0);
    if (ret < 0) return -1;
    if ((int32_t)reply.regs.data[0] < 0) return -1;

    ev->type      = INPUT_UNPACK_TYPE(reply.regs.data[1]);
    ev->modifiers = INPUT_UNPACK_MODIFIERS(reply.regs.data[1]);
    ev->code      = INPUT_UNPACK_CODE(reply.regs.data[1]);
    ev->value     = INPUT_UNPACK_VALUE(reply.regs.data[2]);
    ev->value2    = INPUT_UNPACK_VALUE2(reply.regs.data[2]);
    ev->timestamp = INPUT_UNPACK_TIMESTAMP(reply.regs.data[3]);
    return 0;
}

static int displaydev_write(handle_t ep, const void *buf, size_t len) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = DISPDEV_WRITE;
    msg.regs.data[1] = (uint32_t)len;
    msg.buffer.data  = (uint64_t)(uintptr_t)buf;
    msg.buffer.size  = (uint32_t)len;

    int ret = sys_ipc_call(ep, &msg, &reply, 5000);
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int displaydev_clear(handle_t ep) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = DISPDEV_CLEAR;
    int ret = sys_ipc_call(ep, &msg, &reply, 5000);
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int displaydev_set_cursor(handle_t ep, int row, int col) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = DISPDEV_SET_CURSOR;
    msg.regs.data[1] = (uint32_t)row;
    msg.regs.data[2] = (uint32_t)col;
    int ret = sys_ipc_call(ep, &msg, &reply, 5000);
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int displaydev_set_color(handle_t ep, uint8_t fg, uint8_t bg) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = DISPDEV_SET_COLOR;
    msg.regs.data[1] = (uint32_t)((fg & 0x0F) | ((bg & 0x0F) << 4));
    int ret = sys_ipc_call(ep, &msg, &reply, 5000);
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int displaydev_reset_color(handle_t ep) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = DISPDEV_RESET_COLOR;
    int ret = sys_ipc_call(ep, &msg, &reply, 5000);
    if (ret < 0) return -1;
    return (int)reply.regs.data[0];
}

static int displaydev_info(handle_t ep, int *rows, int *cols) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    char name_buf[64];

    msg.regs.data[0] = DISPDEV_INFO;
    reply.buffer.data = (uint64_t)(uintptr_t)name_buf;
    reply.buffer.size = sizeof(name_buf);

    int ret = sys_ipc_call(ep, &msg, &reply, 1000);
    if (ret < 0)
        return -1;
    if ((int32_t)reply.regs.data[0] < 0)
        return -1;
    if (rows)
        *rows = (int)reply.regs.data[2];
    if (cols)
        *cols = (int)reply.regs.data[3];
    return 0;
}

static uint8_t ansi_to_vga_color(int ansi_code) {
    static const uint8_t standard_colors[8] = {
        0x00, 0x04, 0x02, 0x06, 0x01, 0x05, 0x03, 0x07,
    };
    static const uint8_t bright_colors[8] = {
        0x08, 0x0C, 0x0A, 0x0E, 0x09, 0x0D, 0x0B, 0x0F,
    };

    if (ansi_code >= 30 && ansi_code <= 37) {
        return standard_colors[ansi_code - 30];
    }
    if (ansi_code >= 90 && ansi_code <= 97) {
        return bright_colors[ansi_code - 90];
    }
    return 0x07;
}

static void term_screen_fill(struct terminal *t, char ch, uint8_t fg, uint8_t bg) {
    int total = t->screen_rows * t->screen_cols;
    for (int i = 0; i < total; i++) {
        t->screen[i].ch = ch;
        t->screen[i].fg = fg;
        t->screen[i].bg = bg;
    }
}

static void term_screen_clear(struct terminal *t) {
    if (!t->screen)
        return;
    term_screen_fill(t, ' ', t->cur_fg, t->cur_bg);
    t->cursor_row = 0;
    t->cursor_col = 0;
}

static void term_screen_scroll(struct terminal *t, int lines) {
    if (!t->screen || lines <= 0 || lines > t->screen_rows)
        return;

    int cols = t->screen_cols;
    int rows = t->screen_rows;
    int keep = rows - lines;
    if (keep > 0) {
        memmove(t->screen, t->screen + lines * cols,
                (size_t)(keep * cols) * sizeof(struct term_cell));
    }
    for (int row = keep; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            struct term_cell *cell = &t->screen[row * cols + col];
            cell->ch = ' ';
            cell->fg = t->cur_fg;
            cell->bg = t->cur_bg;
        }
    }
}

static void term_screen_newline(struct terminal *t) {
    t->cursor_col = 0;
    t->cursor_row++;
    if (t->cursor_row >= t->screen_rows) {
        term_screen_scroll(t, 1);
        t->cursor_row = t->screen_rows - 1;
    }
}

static void term_screen_putc(struct terminal *t, char c) {
    if (!t->screen)
        return;

    if (c == '\n') {
        term_screen_newline(t);
        return;
    }
    if (c == '\r') {
        t->cursor_col = 0;
        return;
    }
    if (c == '\t') {
        int next = (t->cursor_col + 8) & ~7;
        if (next >= t->screen_cols)
            term_screen_newline(t);
        else
            t->cursor_col = next;
        return;
    }
    if (c == '\b') {
        if (t->cursor_col > 0) {
            t->cursor_col--;
            t->screen[t->cursor_row * t->screen_cols + t->cursor_col] =
                (struct term_cell){ .ch = ' ', .fg = t->cur_fg, .bg = t->cur_bg };
        }
        return;
    }
    if (c < 32 || c > 126)
        c = '?';

    if (t->cursor_col >= t->screen_cols)
        term_screen_newline(t);

    t->screen[t->cursor_row * t->screen_cols + t->cursor_col] =
        (struct term_cell){ .ch = c, .fg = t->cur_fg, .bg = t->cur_bg };
    t->cursor_col++;
    if (t->cursor_col >= t->screen_cols)
        term_screen_newline(t);
}

static void term_screen_write(struct terminal *t, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        term_screen_putc(t, data[i]);
}

static void term_display_flush(struct terminal *t) {
    if (!t->is_display_vt)
        return;

    displaydev_clear(t->output_ep);
    for (int row = 0; row < t->screen_rows; row++) {
        int col = 0;
        while (col < t->screen_cols) {
            struct term_cell *cell = &t->screen[row * t->screen_cols + col];
            int run = 1;
            while (col + run < t->screen_cols) {
                struct term_cell *next = &t->screen[row * t->screen_cols + col + run];
                if (next->fg != cell->fg || next->bg != cell->bg)
                    break;
                run++;
            }

            char run_buf[256];
            if (run > (int)sizeof(run_buf))
                run = (int)sizeof(run_buf);
            for (int i = 0; i < run; i++)
                run_buf[i] = t->screen[row * t->screen_cols + col + i].ch;

            displaydev_set_cursor(t->output_ep, row, col);
            displaydev_set_color(t->output_ep, cell->fg, cell->bg);
            displaydev_write(t->output_ep, run_buf, (size_t)run);
            col += run;
        }
    }
    displaydev_set_color(t->output_ep, t->cur_fg, t->cur_bg);
    displaydev_set_cursor(t->output_ep, t->cursor_row, t->cursor_col);
}

static void term_activate_display_vt(struct terminal *t) {
    pthread_mutex_lock(&g_display_lock);
    g_active_display_term = t->id;
    term_display_flush(t);
    pthread_mutex_unlock(&g_display_lock);
}

static int term_init_display_vt(struct terminal *t, int id, handle_t term_ep,
                                handle_t input_ep, handle_t output_ep) {
    int rows = 0, cols = 0;
    term_init(t, id, term_ep, input_ep, DEV_INPUTDEV, output_ep, DEV_DISPLAYDEV);
    if (displaydev_info(output_ep, &rows, &cols) < 0 || rows <= 0 || cols <= 0)
        return -1;

    t->is_display_vt = 1;
    t->screen_rows = rows;
    t->screen_cols = cols;
    t->cur_fg = 7;
    t->cur_bg = 0;
    t->screen = calloc((size_t)rows * (size_t)cols, sizeof(struct term_cell));
    if (!t->screen)
        return -1;
    term_screen_clear(t);
    return 0;
}

static void term_output_set_color(struct terminal *t, uint8_t fg, uint8_t bg) {
    t->cur_fg = fg & 0x0F;
    t->cur_bg = bg & 0x0F;
    if (t->output_proto == DEV_DISPLAYDEV) {
        pthread_mutex_lock(&g_display_lock);
        if (g_active_display_term == t->id)
            displaydev_set_color(t->output_ep, t->cur_fg, t->cur_bg);
        pthread_mutex_unlock(&g_display_lock);
    }
}

static void term_output_reset_color(struct terminal *t) {
    t->cur_fg = 7;
    t->cur_bg = 0;
    if (t->output_proto == DEV_DISPLAYDEV) {
        pthread_mutex_lock(&g_display_lock);
        if (g_active_display_term == t->id)
            displaydev_reset_color(t->output_ep);
        pthread_mutex_unlock(&g_display_lock);
    }
}

static void ansi_execute_csi(struct terminal *t, const char *params, char final) {
    int code = 0;
    const char *p = params;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            code = code * 10 + (*p - '0');
        } else if (*p == ';') {
            break;
        }
        p++;
    }

    if (final == 'm') {
        if (code == 0) {
            term_output_reset_color(t);
        } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
            term_output_set_color(t, ansi_to_vga_color(code), 0);
        }
    } else if (final == 'J') {
        if (code == 2 && t->output_proto == DEV_DISPLAYDEV) {
            term_screen_clear(t);
            pthread_mutex_lock(&g_display_lock);
            if (g_active_display_term == t->id) {
                displaydev_clear(t->output_ep);
                displaydev_set_cursor(t->output_ep, 0, 0);
            }
            pthread_mutex_unlock(&g_display_lock);
        }
    } else if (final == 'H') {
        if (t->output_proto == DEV_DISPLAYDEV) {
            t->cursor_row = 0;
            t->cursor_col = 0;
            pthread_mutex_lock(&g_display_lock);
            if (g_active_display_term == t->id)
                displaydev_set_cursor(t->output_ep, 0, 0);
            pthread_mutex_unlock(&g_display_lock);
        }
    }
}

static void term_output_write_parsed(struct terminal *t, const char *data, size_t len) {
    struct ansi_parser *parser = &t->ansi;
    char out_buf[256];
    int out_pos = 0;

    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        switch (parser->state) {
        case ANSI_STATE_NORMAL:
            if (c == '\x1b') {
                if (out_pos > 0) {
                    term_screen_write(t, out_buf, (size_t)out_pos);
                    pthread_mutex_lock(&g_display_lock);
                    if (g_active_display_term == t->id)
                        displaydev_write(t->output_ep, out_buf, (size_t)out_pos);
                    pthread_mutex_unlock(&g_display_lock);
                    out_pos = 0;
                }
                parser->state = ANSI_STATE_ESC;
            } else {
                out_buf[out_pos++] = c;
                if (out_pos == (int)sizeof(out_buf)) {
                    term_screen_write(t, out_buf, (size_t)out_pos);
                    pthread_mutex_lock(&g_display_lock);
                    if (g_active_display_term == t->id)
                        displaydev_write(t->output_ep, out_buf, (size_t)out_pos);
                    pthread_mutex_unlock(&g_display_lock);
                    out_pos = 0;
                }
            }
            break;
        case ANSI_STATE_ESC:
            if (c == '[') {
                parser->state = ANSI_STATE_CSI;
                parser->param_pos = 0;
            } else {
                parser->state = ANSI_STATE_NORMAL;
            }
            break;
        case ANSI_STATE_CSI:
            if ((c >= '0' && c <= '9') || c == ';') {
                if (parser->param_pos < (int)sizeof(parser->param_buf) - 1) {
                    parser->param_buf[parser->param_pos++] = c;
                }
            } else {
                parser->param_buf[parser->param_pos] = '\0';
                ansi_execute_csi(t, parser->param_buf, c);
                parser->state = ANSI_STATE_NORMAL;
            }
            break;
        }
    }

    if (out_pos > 0) {
        term_screen_write(t, out_buf, (size_t)out_pos);
        pthread_mutex_lock(&g_display_lock);
        if (g_active_display_term == t->id)
            displaydev_write(t->output_ep, out_buf, (size_t)out_pos);
        pthread_mutex_unlock(&g_display_lock);
    }

    pthread_mutex_lock(&g_display_lock);
    if (g_active_display_term == t->id)
        displaydev_set_cursor(t->output_ep, t->cursor_row, t->cursor_col);
    pthread_mutex_unlock(&g_display_lock);
}

/* ============== 输出辅助 ============== */

static void term_output_write(struct terminal *t, const void *buf, size_t len) {
    if (t->output_proto == DEV_DISPLAYDEV) {
        term_output_write_parsed(t, (const char *)buf, len);
    } else {
        chardev_write(t->output_ep, buf, len);
    }
}

static void term_output_char(struct terminal *t, char c) {
    term_output_write(t, &c, 1);
}

static void term_output_str(struct terminal *t, const char *s, size_t len) {
    term_output_write(t, s, len);
}

/* ============== 延迟回复 ============== */

static void try_fulfill_pending_read(struct terminal *t) {
    if (!t->pending_read) return;

    int avail = input_available(t);
    if (avail <= 0) return;

    int to_read = avail;
    if ((uint32_t)to_read > t->pending_max_len)
        to_read = (int)t->pending_max_len;

    char buf[TERM_INPUT_BUF_SIZE];
    int  actual = 0;
    for (int i = 0; i < to_read; i++) {
        int c = input_get(t);
        if (c < 0) break;
        buf[actual++] = (char)c;
    }

    struct ipc_message reply = {0};
    reply.regs.data[0] = (uint32_t)actual;
    if (actual > 0) {
        reply.buffer.data = (uint64_t)(uintptr_t)buf;
        reply.buffer.size = (uint32_t)actual;
    }

    sys_ipc_reply_to(t->pending_tid, &reply);
    t->pending_read = 0;
}

/* ============== 行规程 ============== */

static void term_process_input(struct terminal *t, char c) {
    /* Ctrl+C */
    if (c == 0x03) {
        if (t->foreground_pid > 0)
            sys_kill(t->foreground_pid, SIGINT);
        return;
    }

    if (t->mode == LDISC_RAW) {
        pthread_mutex_lock(&t->input_lock);
        input_put(t, c);
        try_fulfill_pending_read(t);
        pthread_mutex_unlock(&t->input_lock);

        if (t->echo)
            term_output_char(t, c);
        return;
    }

    /* Cooked 模式 */
    if (c == '\b' || c == 0x7F) {
        if (t->line_pos > 0) {
            t->line_pos--;
            if (t->echo)
                term_output_str(t, "\b \b", 3);
        }
        return;
    }

    if (c == '\r' || c == '\n') {
        if (t->echo)
            term_output_char(t, '\n');

        pthread_mutex_lock(&t->input_lock);
        for (uint16_t i = 0; i < t->line_pos; i++)
            input_put(t, t->line_buf[i]);
        input_put(t, '\n');
        t->line_pos = 0;
        try_fulfill_pending_read(t);
        pthread_mutex_unlock(&t->input_lock);
        return;
    }

    if (c >= 32 && c < 127) {
        if (t->line_pos < TERM_LINE_BUF_SIZE - 1) {
            t->line_buf[t->line_pos++] = c;
            if (t->echo)
                term_output_char(t, c);
        }
    }
}

/* ============== 输入线程 ============== */

static int event_to_chars(const struct input_event *ev, char *out, int max) {
    if (ev->type != INPUT_EVENT_KEY_PRESS)
        return 0;

    uint16_t code = ev->code;

    if (code == INPUT_KEY_UP && max >= 3) {
        out[0] = 0x1b;
        out[1] = '[';
        out[2] = 'A';
        return 3;
    }
    if (code == INPUT_KEY_DOWN && max >= 3) {
        out[0] = 0x1b;
        out[1] = '[';
        out[2] = 'B';
        return 3;
    }
    if (code == INPUT_KEY_RIGHT && max >= 3) {
        out[0] = 0x1b;
        out[1] = '[';
        out[2] = 'C';
        return 3;
    }
    if (code == INPUT_KEY_LEFT && max >= 3) {
        out[0] = 0x1b;
        out[1] = '[';
        out[2] = 'D';
        return 3;
    }

    /* 普通可打印字符 (code 字段存的是 ASCII 码) */
    if (code > 0 && code < 128 && max >= 1) {
        out[0] = (char)code;
        return 1;
    }

    return 0;
}

static void *input_thread(void *arg) {
    struct terminal *t = (struct terminal *)arg;

    if (t->input_proto == DEV_INPUTDEV) {
        /* inputdev 路径: 读取 input_event → 转换为字符 */
        while (1) {
            struct input_event ev;
            if (inputdev_read_event(t->input_ep, &ev) < 0) {
                msleep(100);
                continue;
            }

            char chars[8];
            int n = event_to_chars(&ev, chars, sizeof(chars));
            for (int i = 0; i < n; i++)
                term_process_input(t, chars[i]);
        }
    } else {
        /* chardev 路径: 直接读取字节流 */
        while (1) {
            char buf[64];
            int  n = chardev_read(t->input_ep, buf, sizeof(buf));
            if (n <= 0) {
                msleep(100);
                continue;
            }
            for (int i = 0; i < n; i++)
                term_process_input(t, buf[i]);
        }
    }
    return NULL;
}

static void *display_input_thread(void *arg) {
    struct terminal *source = (struct terminal *)arg;

    while (1) {
        pthread_mutex_lock(&g_display_lock);
        int active_id = g_active_display_term;
        pthread_mutex_unlock(&g_display_lock);
        if (active_id < 0) {
            msleep(20);
            continue;
        }

        struct input_event ev;
        if (inputdev_read_event(source->input_ep, &ev) < 0) {
            msleep(100);
            continue;
        }

        char chars[8];
        int n = event_to_chars(&ev, chars, sizeof(chars));
        if (n <= 0)
            continue;

        pthread_mutex_lock(&g_display_lock);
        struct terminal *active = find_term_by_id(g_active_display_term);
        pthread_mutex_unlock(&g_display_lock);
        if (!active)
            continue;

        for (int i = 0; i < n; i++)
            term_process_input(active, chars[i]);
    }

    return NULL;
}

/* ============== 服务线程 ============== */

static int term_handle_msg(struct terminal *t, struct ipc_message *msg,
                           char *reply_buf) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case TTY_OP_INPUT: {
        char *data = (char *)(uintptr_t)msg->buffer.data;
        uint32_t size = msg->buffer.size;
        if (data && size > 0) {
            for (uint32_t i = 0; i < size; i++) {
                term_process_input(t, data[i]);
            }
        }
        msg->regs.data[0] = 0;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;
    }

    case IO_WRITE: {
        uint32_t size = msg->regs.data[3];
        char    *data = (char *)(uintptr_t)msg->buffer.data;
        if (data && size > 0) {
            term_output_write(t, data, size);
        }
        msg->regs.data[0] = (uint32_t)size;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;
    }

    case IO_READ: {
        uint32_t max_len = msg->regs.data[3];
        if (max_len == 0) max_len = 1;

        pthread_mutex_lock(&t->input_lock);
        int avail = input_available(t);
        if (avail > 0) {
            int to_read = avail;
            if ((uint32_t)to_read > max_len)
                to_read = (int)max_len;

            int actual = 0;
            for (int i = 0; i < to_read; i++) {
                int c = input_get(t);
                if (c < 0) break;
                reply_buf[actual++] = (char)c;
            }
            pthread_mutex_unlock(&t->input_lock);

            msg->regs.data[0] = (uint32_t)actual;
            msg->buffer.data = (uint64_t)(uintptr_t)reply_buf;
            msg->buffer.size = (uint32_t)actual;
            return 0;
        }

        /* 延迟回复: 等待输入 */
        t->pending_read    = 1;
        t->pending_tid     = msg->sender_tid;
        t->pending_max_len = max_len;
        pthread_mutex_unlock(&t->input_lock);
        return 1; /* NOREPLY */
    }

    case IO_CLOSE:
        msg->regs.data[0] = 0;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;

    case IO_IOCTL: {
        uint32_t cmd = msg->regs.data[2];
        switch (cmd) {
        case TTY_IOCTL_SET_FOREGROUND:
            t->foreground_pid = (int)msg->regs.data[3];
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_GET_FOREGROUND:
            msg->regs.data[0] = (uint32_t)t->foreground_pid;
            break;
        case TTY_IOCTL_SET_RAW:
            t->mode = LDISC_RAW;
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_SET_COOKED:
            t->mode = LDISC_COOKED;
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_SET_ECHO:
            t->echo = (int)msg->regs.data[3];
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_GET_TTY_COUNT:
            msg->regs.data[0] = (uint32_t)g_term_count;
            break;
        case TTY_IOCTL_SET_ACTIVE_TTY: {
            if (msg->regs.data[3] == UINT32_MAX) {
                pthread_mutex_lock(&g_display_lock);
                g_active_display_term = -1;
                pthread_mutex_unlock(&g_display_lock);
                msg->regs.data[0] = 0;
                break;
            }
            struct terminal *target = find_term_by_id((int)msg->regs.data[3]);
            if (!target || !target->is_display_vt) {
                msg->regs.data[0] = (uint32_t)-1;
                break;
            }
            term_activate_display_vt(target);
            msg->regs.data[0] = 0;
            break;
        }
        case TTY_IOCTL_GET_ACTIVE_TTY:
            pthread_mutex_lock(&g_display_lock);
            msg->regs.data[0] = (uint32_t)g_active_display_term;
            pthread_mutex_unlock(&g_display_lock);
            break;
        case TTY_IOCTL_FLUSH_INPUT:
            pthread_mutex_lock(&t->input_lock);
            t->input_head    = 0;
            t->input_tail    = 0;
            t->line_pos      = 0;
            t->pending_read  = 0;
            t->pending_tid   = 0;
            t->pending_max_len = 0;
            pthread_mutex_unlock(&t->input_lock);
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_SET_COLOR:
            term_output_set_color(t, (uint8_t)msg->regs.data[3], (uint8_t)msg->regs.data[4]);
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_RESET_COLOR:
            term_output_reset_color(t);
            msg->regs.data[0] = 0;
            break;
        default:
            msg->regs.data[0] = (uint32_t)-1;
            break;
        }
        return 0;
    }

    default:
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }
}

static void *service_thread(void *arg) {
    struct terminal *t = (struct terminal *)arg;
    char recv_buf[4096];
    while (1) {
        struct ipc_message msg = {0};
        msg.buffer.data = (uint64_t)(uintptr_t)recv_buf;
        msg.buffer.size = sizeof(recv_buf);

        if (sys_ipc_receive(t->term_ep, &msg, 0) < 0)
            continue;

        int ret = term_handle_msg(t, &msg, recv_buf);
        bool noreply = (msg.flags & ABI_IPC_FLAG_NOREPLY) != 0;
        if (ret == 0 && !noreply)
            sys_ipc_reply(&msg);
        /* ret == 1: 延迟回复 (pending_read) */
    }
    return NULL;
}

/* ============== 终端初始化 ============== */

static void term_init(struct terminal *t, int id, handle_t term_ep,
                      handle_t input_ep, enum dev_proto input_proto,
                      handle_t output_ep, enum dev_proto output_proto) {
    memset(t, 0, sizeof(*t));
    t->id           = id;
    t->term_ep      = term_ep;
    t->input_ep     = input_ep;
    t->input_proto  = input_proto;
    t->output_ep    = output_ep;
    t->output_proto = output_proto;

    t->mode = LDISC_RAW;
    t->echo = 0;

    pthread_mutex_init(&t->input_lock, NULL);
}

static handle_t get_or_create_tty_ep(const char *name) {
    handle_t ep = env_get_handle(name);
    if (ep == HANDLE_INVALID)
        ep = sys_endpoint_create(name);
    return ep;
}

/* ============== 入口 ============== */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* term 自身的 stdio 走 debug fallback, 避免循环 */
    _stdio_set_fd(stdout, -1);
    _stdio_set_fd(stderr, -1);

    pthread_mutex_init(&g_display_lock, NULL);
    handle_t serial_ep    = env_get_handle("serial");     /* 写 endpoint */
    handle_t serial_in_ep = env_get_handle("serial_in");  /* 读 endpoint */
    handle_t kbd_ep       = env_get_handle("kbd_ep");
    handle_t devfs_ep     = env_get_handle("devfs_ep");

    ulog_tagf(stdout, TERM_COLOR_WHITE, "[term]",
              " startup serial=%u serial_in=%u kbd=%u devfs=%u\n",
              serial_ep, serial_in_ep, kbd_ep, devfs_ep);

    /* tty1 (serial 终端): input=serial_in(read), output=serial(write) */
    if (serial_ep != HANDLE_INVALID && serial_in_ep != HANDLE_INVALID) {
        handle_t tty1_ep = get_or_create_tty_ep(ABI_TTY1_HANDLE_NAME);

        if (tty1_ep != HANDLE_INVALID) {
            term_init(&g_terms[g_term_count], 1, tty1_ep,
                      serial_in_ep, DEV_CHARDEV,
                      serial_ep, DEV_CHARDEV);
            g_term_count++;
            ulog_tagf(stdout, TERM_COLOR_WHITE, "[term]",
                      " created tty1 input=%u output=%u\n", serial_in_ep, serial_ep);
        }
    }

    for (int serial_idx = 1; serial_idx < SERIAL_TERM_COUNT && g_term_count < MAX_TERMINALS;
         serial_idx++) {
        char read_name[16];
        char write_name[16];
        char tty_name[16];
        handle_t read_ep;
        handle_t write_ep;
        handle_t tty_ep;
        int tty_id = serial_idx + 1;

        snprintf(read_name, sizeof(read_name), "com_in%d", serial_idx);
        snprintf(write_name, sizeof(write_name), "com%d", serial_idx);
        snprintf(tty_name, sizeof(tty_name), "tty%d", tty_id);

        read_ep = sys_handle_find(read_name);
        write_ep = sys_handle_find(write_name);
        ulog_tagf(stdout, TERM_COLOR_WHITE, "[term]",
                  " probe tty%d read=%s:%u write=%s:%u\n",
                  tty_id, read_name, read_ep, write_name, write_ep);
        if (read_ep == HANDLE_INVALID || write_ep == HANDLE_INVALID) {
            continue;
        }

        tty_ep = get_or_create_tty_ep(tty_name);
        if (tty_ep == HANDLE_INVALID) {
            continue;
        }

        term_init(&g_terms[g_term_count], tty_id, tty_ep,
                  read_ep, DEV_CHARDEV,
                  write_ep, DEV_CHARDEV);
        g_term_count++;
        ulog_tagf(stdout, TERM_COLOR_WHITE, "[term]",
                  " created tty%d input=%u output=%u\n", tty_id, read_ep, write_ep);
    }

    /* tty0/tty5/tty6 (VGA 终端): 共享显示设备和键盘, 前台切换由 term 管理 */
    handle_t fbcon_ep = env_get_handle("fbcon_ep");
    if (kbd_ep != HANDLE_INVALID && fbcon_ep != HANDLE_INVALID) {
        static const struct {
            int id;
            const char *name;
        } display_ttys[] = {
            {0, ABI_TTY0_HANDLE_NAME},
            {5, ABI_TTY5_HANDLE_NAME},
            {6, ABI_TTY6_HANDLE_NAME},
        };

        for (size_t i = 0; i < sizeof(display_ttys) / sizeof(display_ttys[0]) &&
                           g_term_count < MAX_TERMINALS; i++) {
            handle_t tty_ep = get_or_create_tty_ep(display_ttys[i].name);
            if (tty_ep == HANDLE_INVALID)
                continue;
            if (term_init_display_vt(&g_terms[g_term_count], display_ttys[i].id,
                                     tty_ep, kbd_ep, fbcon_ep) < 0) {
                continue;
            }
            ulog_tagf(stdout, TERM_COLOR_WHITE, "[term]",
                      " created tty%d input=%u output=%u\n",
                      display_ttys[i].id, kbd_ep, fbcon_ep);
            g_term_count++;
        }
    } else {
        /* 显示设备不可用时仍创建 endpoint 供依赖解析 */
        if (env_get_handle(ABI_TTY0_HANDLE_NAME) == HANDLE_INVALID)
            sys_endpoint_create(ABI_TTY0_HANDLE_NAME);
        if (env_get_handle(ABI_TTY5_HANDLE_NAME) == HANDLE_INVALID)
            sys_endpoint_create(ABI_TTY5_HANDLE_NAME);
        if (env_get_handle(ABI_TTY6_HANDLE_NAME) == HANDLE_INVALID)
            sys_endpoint_create(ABI_TTY6_HANDLE_NAME);
    }

    /* 启动各终端的输入线程 + 服务线程 */
    for (int i = 0; i < g_term_count; i++) {
        pthread_t stid;
        pthread_create(&stid, NULL, service_thread, &g_terms[i]);

        if (!g_terms[i].is_display_vt) {
            pthread_t tid;
            pthread_create(&tid, NULL, input_thread, &g_terms[i]);
        }
    }

    for (int i = 0; i < g_term_count; i++) {
        if (g_terms[i].is_display_vt) {
            pthread_t tid;
            pthread_create(&tid, NULL, display_input_thread, &g_terms[i]);
            break;
        }
    }

    if (devfs_ep != HANDLE_INVALID) {
        for (int i = 0; i < g_term_count; i++) {
            register_tty_device(devfs_ep, g_terms[i].id, g_terms[i].term_ep);
        }
    }

    ulog_tagf(stdout, TERM_COLOR_WHITE, "[term]",
              " about to notify ready (%d terminals)\n", g_term_count);

    svc_notify_ready("term");

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[term]",
              " ready (%d terminals)\n", g_term_count);

    /* 主线程保持进程存活 */
    while (1) {
        msleep(10000);
    }

    return 0;
}
