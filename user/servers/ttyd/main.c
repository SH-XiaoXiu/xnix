/**
 * @file ttyd/main.c
 * @brief 用户态终端服务器
 *
 * 管理多个 tty 实例(tty0=VGA, tty1=Serial),每个 tty 有独立的
 * 输入队列,行规程和前台进程.
 *
 * 架构:
 *   kbd → TTY_OP_INPUT → ttyd(tty0) → fbd (输出)
 *   seriald → TTY_OP_INPUT → ttyd(tty1) → seriald (输出)
 *   shell → TTY_OP_WRITE/READ → ttyd(ttyN) → 设备驱动
 */

#include <d/protocol/serial.h>
#include <d/protocol/tty.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/tty.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/ipc/console.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* Debug write helper */
static inline void sys_debug_write(const char *buf, size_t len) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(SYS_DEBUG_WRITE), "b"((uint32_t)(uintptr_t)buf), "c"((uint32_t)len)
                 : "memory");
    (void)ret;
}

/* 行规程模式 */
enum ldisc_mode {
    LDISC_RAW    = 0, /* 直通:输入直接给应用 */
    LDISC_COOKED = 1, /* 行编辑:缓冲到换行才交给应用 */
};

/* 行规程状态 */
struct line_discipline {
    enum ldisc_mode mode;
    int             echo;

    /* cooked 模式行缓冲 */
    char     line_buf[TTY_INPUT_BUF_SIZE];
    uint16_t line_pos;

    /* 行完成标志 */
    int line_ready;
};

/* TTY 实例 */
struct tty_instance {
    int      id;
    handle_t endpoint;  /* 此 tty 的 endpoint (tty0, tty1) */
    handle_t output_ep; /* 输出设备 endpoint (serial, fbd) */
    handle_t fallback_output_ep;
    handle_t primary_output_ep; /* 原始输出设备(用于重试) */
    int      fallback_count;    /* 连续 fallback 次数 */
    handle_t input_ep;          /* 输入设备 endpoint (kbd_ep, serial) */

    /* 输入队列 */
    char             input_buf[TTY_INPUT_BUF_SIZE];
    volatile uint8_t input_head;
    volatile uint8_t input_tail;
    pthread_mutex_t  input_lock;

    /* 等待输入的 pending read 请求 */
    int      pending_read;    /* 是否有等待的读请求 */
    uint32_t pending_tid;     /* 等待读请求的线程 ID */
    uint32_t pending_max_len; /* 请求的最大长度 */

    /* 行规程 */
    struct line_discipline ldisc;

    /* 前台进程 */
    int foreground_pid;
};

#define MAX_TTY               2
#define TTY_OUTPUT_TIMEOUT_MS 50
static struct tty_instance g_ttys[MAX_TTY];
static int                 g_tty_count = 0;

/* 向输入队列写入一个字符 */
static int tty_input_put(struct tty_instance *tty, char c) {
    uint8_t next = (tty->input_head + 1) % TTY_INPUT_BUF_SIZE;
    if (next == tty->input_tail) {
        return -1; /* 满 */
    }
    tty->input_buf[tty->input_head] = c;
    tty->input_head                 = next;
    return 0;
}

/* 从输入队列读取一个字符 */
static int tty_input_get(struct tty_instance *tty) {
    if (tty->input_head == tty->input_tail) {
        return -1; /* 空 */
    }
    char c          = tty->input_buf[tty->input_tail];
    tty->input_tail = (tty->input_tail + 1) % TTY_INPUT_BUF_SIZE;
    return (unsigned char)c;
}

/* 输入队列中可用字符数 */
static int tty_input_available(struct tty_instance *tty) {
    if (tty->input_head >= tty->input_tail) {
        return tty->input_head - tty->input_tail;
    }
    return TTY_INPUT_BUF_SIZE - tty->input_tail + tty->input_head;
}

/**
 * 发送消息到输出设备,失败时使用 fallback,定期重试 primary
 */
static int tty_output_send(struct tty_instance *tty, struct ipc_message *msg) {
    if (tty->output_ep == HANDLE_INVALID) {
        return -1;
    }

    /* 每 32 次 fallback 后尝试恢复 primary */
    if (tty->fallback_count > 0 && (tty->fallback_count % 32) == 0 &&
        tty->primary_output_ep != HANDLE_INVALID && tty->output_ep != tty->primary_output_ep) {
        int probe = sys_ipc_send(tty->primary_output_ep, msg, TTY_OUTPUT_TIMEOUT_MS);
        if (probe == 0) {
            tty->output_ep      = tty->primary_output_ep;
            tty->fallback_count = 0;
            return 0;
        }
    }

    int ret = sys_ipc_send(tty->output_ep, msg, TTY_OUTPUT_TIMEOUT_MS);

    if (ret == 0) {
        if (tty->output_ep == tty->primary_output_ep) {
            tty->fallback_count = 0;
        }
        return 0;
    }

    /* fallback */
    if (tty->fallback_output_ep != HANDLE_INVALID && tty->output_ep != tty->fallback_output_ep) {
        tty->output_ep = tty->fallback_output_ep;
        tty->fallback_count++;
        return sys_ipc_send(tty->output_ep, msg, TTY_OUTPUT_TIMEOUT_MS);
    }
    return ret;
}

/* 向输出设备写一个字符 */
static void tty_output_char(struct tty_instance *tty, char c) {
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_PUTC;
    msg.regs.data[1] = (uint32_t)(unsigned char)c;
    tty_output_send(tty, &msg);
}

/* 向输出设备写字符串(通过 WRITE 批量发送) */
static void tty_output_write(struct tty_instance *tty, const char *data, int len) {
    if (tty->output_ep == HANDLE_INVALID || len <= 0) {
        return;
    }

    while (len > 0) {
        struct ipc_message msg;
        memset(&msg, 0, sizeof(msg));
        msg.regs.data[0] = UDM_CONSOLE_WRITE;

        int chunk = len;
        if (chunk > UDM_CONSOLE_WRITE_MAX) {
            chunk = UDM_CONSOLE_WRITE_MAX;
        }

        memcpy(&msg.regs.data[1], data, chunk);
        msg.regs.data[7] = (uint32_t)chunk;

        if (tty_output_send(tty, &msg) < 0) {
            break;
        }

        data += chunk;
        len -= chunk;
    }
}

static void tty_output_set_color(struct tty_instance *tty, uint8_t fg, uint8_t bg) {
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_SET_COLOR;
    msg.regs.data[1] = (uint32_t)((fg & 0x0F) | ((bg & 0x0F) << 4));
    tty_output_send(tty, &msg);
}

static void tty_output_reset_color(struct tty_instance *tty) {
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_RESET_COLOR;
    tty_output_send(tty, &msg);
}

/* 尝试满足挂起的读请求 */
static void try_fulfill_pending_read(struct tty_instance *tty) {
    if (!tty->pending_read) {
        return;
    }

    int avail = tty_input_available(tty);
    if (avail <= 0) {
        return;
    }

    /* 读取可用数据 */
    int to_read = avail;
    if ((uint32_t)to_read > tty->pending_max_len) {
        to_read = (int)tty->pending_max_len;
    }

    struct ipc_message reply;
    memset(&reply, 0, sizeof(reply));

    /* 将数据打包到回复中 */
    char buf[TTY_INPUT_BUF_SIZE];
    int  actual_read = 0;
    for (int i = 0; i < to_read; i++) {
        int c = tty_input_get(tty);
        if (c < 0) {
            break;
        }
        buf[actual_read++] = (char)c;
    }

    reply.regs.data[0] = (uint32_t)actual_read;
    if (actual_read > 0) {
        reply.buffer.data = (uint64_t)(uintptr_t)buf;
        reply.buffer.size = (uint32_t)actual_read;
    }

    sys_ipc_reply_to(tty->pending_tid, &reply);
    tty->pending_read = 0;
}

/* 处理输入字符(经过行规程) */
static void tty_process_input(struct tty_instance *tty, char c) {
    struct line_discipline *ld = &tty->ldisc;

    /* Ctrl+C:发送 SIGINT 给前台进程 */
    if (c == 0x03) {
        if (tty->foreground_pid > 0) {
            sys_kill(tty->foreground_pid, SIGINT);
        }
        return;
    }

    if (ld->mode == LDISC_RAW) {
        /* Raw 模式:直接放入输入队列 */
        pthread_mutex_lock(&tty->input_lock);
        tty_input_put(tty, c);
        try_fulfill_pending_read(tty);
        pthread_mutex_unlock(&tty->input_lock);

        /* Echo */
        if (ld->echo) {
            tty_output_char(tty, c);
        }
        return;
    }

    /* Cooked 模式 */
    if (c == '\b' || c == 0x7F) {
        /* Backspace */
        if (ld->line_pos > 0) {
            ld->line_pos--;
            if (ld->echo) {
                tty_output_write(tty, "\b \b", 3);
            }
        }
        return;
    }

    if (c == 0x04) {
        /* Ctrl+D:行首时 EOF,否则 flush */
        if (ld->line_pos == 0) {
            /* EOF:放入空读取 */
            pthread_mutex_lock(&tty->input_lock);
            try_fulfill_pending_read(tty);
            pthread_mutex_unlock(&tty->input_lock);
        } else {
            /* Flush 当前行缓冲 */
            pthread_mutex_lock(&tty->input_lock);
            for (uint16_t i = 0; i < ld->line_pos; i++) {
                tty_input_put(tty, ld->line_buf[i]);
            }
            ld->line_pos = 0;
            try_fulfill_pending_read(tty);
            pthread_mutex_unlock(&tty->input_lock);
        }
        return;
    }

    if (c == '\r' || c == '\n') {
        /* Enter:将行放入输入队列 */
        if (ld->echo) {
            tty_output_char(tty, '\n');
        }

        pthread_mutex_lock(&tty->input_lock);
        for (uint16_t i = 0; i < ld->line_pos; i++) {
            tty_input_put(tty, ld->line_buf[i]);
        }
        tty_input_put(tty, '\n');
        ld->line_pos = 0;
        try_fulfill_pending_read(tty);
        pthread_mutex_unlock(&tty->input_lock);
        return;
    }

    /* 普通字符:添加到行缓冲 */
    if (ld->line_pos < TTY_INPUT_BUF_SIZE - 1) {
        ld->line_buf[ld->line_pos++] = c;
        if (ld->echo) {
            tty_output_char(tty, c);
        }
    }
}

/* 处理一个 tty endpoint 上的消息 */
static int tty_handle_msg(struct tty_instance *tty, struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case TTY_OP_WRITE: {
        /* 写输出到设备 */
        int   len  = (int)msg->regs.data[1];
        char *data = (char *)(uintptr_t)msg->buffer.data;
        if (data && len > 0) {
            tty_output_write(tty, data, len);
        }
        msg->regs.data[0] = (uint32_t)len;
        return 0;
    }

    case TTY_OP_PUTC: {
        char c = (char)msg->regs.data[1];
        tty_output_char(tty, c);
        msg->regs.data[0] = 1;
        return 0;
    }

    case TTY_OP_READ: {
        uint32_t max_len = msg->regs.data[1];
        if (max_len == 0) {
            max_len = 1;
        }

        pthread_mutex_lock(&tty->input_lock);
        int avail = tty_input_available(tty);
        if (avail > 0) {
            /* 立即返回 */
            int to_read = avail;
            if ((uint32_t)to_read > max_len) {
                to_read = (int)max_len;
            }
            char buf[TTY_INPUT_BUF_SIZE];
            for (int i = 0; i < to_read; i++) {
                int c = tty_input_get(tty);
                if (c < 0) {
                    to_read = i;
                    break;
                }
                buf[i] = (char)c;
            }
            pthread_mutex_unlock(&tty->input_lock);
            msg->regs.data[0] = (uint32_t)to_read;
            msg->buffer.data  = (uint64_t)(uintptr_t)buf;
            msg->buffer.size  = (uint32_t)to_read;
            return 0;
        }

        /* 延迟回复:等待输入 */
        tty->pending_read    = 1;
        tty->pending_tid     = msg->sender_tid;
        tty->pending_max_len = max_len;
        pthread_mutex_unlock(&tty->input_lock);
        return 1; /* 延迟回复 */
    }

    case TTY_OP_INPUT: {
        /* 从输入设备推送的字符 */
        char c = (char)msg->regs.data[1];
        tty_process_input(tty, c);
        msg->regs.data[0] = 0;
        return 0;
    }

    case TTY_OP_IOCTL: {
        uint32_t cmd = msg->regs.data[1];
        switch (cmd) {
        case TTY_IOCTL_SET_FOREGROUND:
            tty->foreground_pid = (int)msg->regs.data[2];
            msg->regs.data[0]   = 0;
            break;
        case TTY_IOCTL_GET_FOREGROUND:
            msg->regs.data[0] = (uint32_t)tty->foreground_pid;
            break;
        case TTY_IOCTL_SET_RAW:
            tty->ldisc.mode   = LDISC_RAW;
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_SET_COOKED:
            tty->ldisc.mode   = LDISC_COOKED;
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_SET_ECHO:
            tty->ldisc.echo   = (int)msg->regs.data[2];
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_GET_TTY_COUNT:
            msg->regs.data[0] = (uint32_t)g_tty_count;
            break;
        case TTY_IOCTL_SET_COLOR:
            tty_output_set_color(tty, (uint8_t)msg->regs.data[2], (uint8_t)msg->regs.data[3]);
            msg->regs.data[0] = 0;
            break;
        case TTY_IOCTL_RESET_COLOR:
            tty_output_reset_color(tty);
            msg->regs.data[0] = 0;
            break;
        default:
            msg->regs.data[0] = (uint32_t)-1;
            break;
        }
        return 0;
    }

    case TTY_OP_OPEN:
    case TTY_OP_CLOSE:
        msg->regs.data[0] = 0;
        return 0;

    default:
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }
}

/**
 * 输入监听线程:从 kbd 读取字符并转发到 tty
 *
 * seriald 将 UART 输入转发给 kbd,所以 kbd 是所有输入的汇聚点.
 * 此线程通过 CONSOLE_OP_GETC 阻塞读取 kbd,收到字符后交给行规程处理.
 */
static void *input_listener_thread(void *arg) {
    struct tty_instance *tty = (struct tty_instance *)arg;

    if (tty->input_ep == HANDLE_INVALID) {
        return NULL;
    }

    while (1) {
        struct ipc_message req;
        struct ipc_message reply;

        memset(&req, 0, sizeof(req));
        memset(&reply, 0, sizeof(reply));

        req.regs.data[0] = CONSOLE_OP_GETC;

        int ret = sys_ipc_call(tty->input_ep, &req, &reply, 0);
        if (ret != 0) {
            msleep(100);
            continue;
        }

        int c = (int)reply.regs.data[0];
        if (c >= 0 && c <= 255) {
            tty_process_input(tty, (char)c);
        }
    }
    return NULL;
}

/* TTY 服务线程:每个 tty 一个线程 */
static void *tty_thread(void *arg) {
    struct tty_instance *tty = (struct tty_instance *)arg;

    struct ipc_message msg;
    char               recv_buf[4096];

    while (1) {
        memset(&msg, 0, sizeof(msg));
        msg.buffer.data = (uint64_t)(uintptr_t)recv_buf;
        msg.buffer.size = sizeof(recv_buf);

        if (sys_ipc_receive(tty->endpoint, &msg, 0) < 0) {
            continue;
        }

        int ret = tty_handle_msg(tty, &msg);
        if (ret == 0) {
            sys_ipc_reply(&msg);
        }
    }
    return NULL;
}

/* 初始化一个 tty 实例 */
static void tty_init_instance(struct tty_instance *tty, int id, handle_t ep, handle_t output,
                              handle_t fallback_output, handle_t input) {
    memset(tty, 0, sizeof(*tty));
    tty->id                 = id;
    tty->endpoint           = ep;
    tty->output_ep          = output;
    tty->primary_output_ep  = output;
    tty->fallback_output_ep = fallback_output;
    tty->fallback_count     = 0;
    tty->input_ep           = input;
    tty->input_head         = 0;
    tty->input_tail         = 0;
    tty->pending_read       = 0;
    tty->foreground_pid     = 0;

    /* 默认 raw 模式,echo 关闭(应用层负责回显) */
    tty->ldisc.mode     = LDISC_RAW;
    tty->ldisc.echo     = 0;
    tty->ldisc.line_pos = 0;

    pthread_mutex_init(&tty->input_lock, NULL);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 强制 stdout/stderr 使用 debug fallback,避免 printf 发给自己死锁 */
    _stdio_force_debug_mode();

    handle_t serial_ep = env_get_handle("serial");
    handle_t kbd_ep    = env_get_handle("kbd_ep");

    if (serial_ep == HANDLE_INVALID) {
        return 1;
    }

    /* 获取 tty1 (serial) endpoint */
    handle_t tty1_ep = env_get_handle(ABI_TTY1_HANDLE_NAME);
    if (tty1_ep == HANDLE_INVALID) {
        tty1_ep = sys_endpoint_create(ABI_TTY1_HANDLE_NAME);
        if (tty1_ep == HANDLE_INVALID) {
            return 1;
        }
    }

    /* 初始化 tty1 (serial 终端) */
    tty_init_instance(&g_ttys[0], 1, tty1_ep, serial_ep, serial_ep, kbd_ep);
    g_tty_count = 1;

    /* 如果 kbd 可用,创建 tty0 (VGA 终端) */
    if (kbd_ep != HANDLE_INVALID) {
        handle_t tty0_ep = env_get_handle(ABI_TTY0_HANDLE_NAME);
        if (tty0_ep == HANDLE_INVALID) {
            tty0_ep = sys_endpoint_create(ABI_TTY0_HANDLE_NAME);
        }
        if (tty0_ep != HANDLE_INVALID) {
            handle_t fbcon_ep = env_get_handle("fbcon_ep");
            handle_t vga_ep   = env_get_handle("vga_ep");

            handle_t output_ep;
            if (fbcon_ep != HANDLE_INVALID) {
                output_ep = fbcon_ep;
            } else if (vga_ep != HANDLE_INVALID) {
                output_ep = vga_ep;
            } else {
                output_ep = serial_ep;
            }

            tty_init_instance(&g_ttys[1], 0, tty0_ep, output_ep, serial_ep, kbd_ep);
            g_tty_count = 2;
        }
    }

    /* 启动输入监听线程 */
    if (kbd_ep != HANDLE_INVALID) {
        struct tty_instance *input_tty = NULL;
        for (int i = 0; i < g_tty_count; i++) {
            if (g_ttys[i].id == 0 && g_ttys[i].input_ep != HANDLE_INVALID) {
                input_tty = &g_ttys[i];
                break;
            }
        }
        if (!input_tty) {
            input_tty = &g_ttys[0];
        }
        pthread_t input_tid;
        pthread_create(&input_tid, NULL, input_listener_thread, input_tty);
    }

    /* 为每个 tty 创建服务线程(跳过 index 0,主线程负责) */
    for (int i = 1; i < g_tty_count; i++) {
        pthread_t tid;
        pthread_create(&tid, NULL, tty_thread, &g_ttys[i]);
    }

    svc_notify_ready("ttyd");

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[ttyd]", " ready (%d ttys)\n", g_tty_count);

    /* 主线程服务 tty1 (serial) */
    tty_thread(&g_ttys[0]);

    return 0;
}
