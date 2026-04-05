/**
 * @file termd/main.c
 * @brief 终端服务
 *
 * 组合 chardev 输入/输出设备为双向终端流.
 * shell 通过 IO_READ/IO_WRITE 与 termd 通信,
 * termd 通过 CHARDEV_READ/WRITE 与底层设备通信.
 *
 * 架构 (每个终端实例):
 *   input_thread: CHARDEV_READ(input_ep) → 行规程 → input_queue
 *   service_thread: IO_READ/IO_WRITE(term_ep) ↔ input_queue / CHARDEV_WRITE(output_ep)
 */

#include <xnix/abi/io.h>
#include <xnix/abi/tty.h>
#include <xnix/protocol/chardev.h>
#include <xnix/protocol/tty.h>

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
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

enum ldisc_mode {
    LDISC_RAW    = 0,
    LDISC_COOKED = 1,
};

struct terminal {
    int      id;
    handle_t term_ep;      /* shell 连接的 endpoint */
    handle_t input_ep;     /* 输入设备 (chardev) */
    handle_t output_ep;    /* 输出设备 (chardev) */

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
};

#define MAX_TERMINALS 4
static struct terminal g_terms[MAX_TERMINALS];
static int             g_term_count = 0;

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

/* ============== 输出辅助 ============== */

static void term_output_char(struct terminal *t, char c) {
    chardev_write(t->output_ep, &c, 1);
}

static void term_output_str(struct terminal *t, const char *s, size_t len) {
    chardev_write(t->output_ep, s, len);
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

static void *input_thread(void *arg) {
    struct terminal *t = (struct terminal *)arg;

    while (1) {
        char buf[64];
        int  n = chardev_read(t->input_ep, buf, sizeof(buf));
        if (n <= 0) {
            msleep(100);
            continue;
        }

        for (int i = 0; i < n; i++) {
            term_process_input(t, buf[i]);
        }
    }
    return NULL;
}

/* ============== 服务线程 ============== */

static int term_handle_msg(struct terminal *t, struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case IO_WRITE: {
        uint32_t size = msg->regs.data[3];
        char    *data = (char *)(uintptr_t)msg->buffer.data;
        if (data && size > 0) {
            chardev_write(t->output_ep, data, size);
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

            char buf[TERM_INPUT_BUF_SIZE];
            int  actual = 0;
            for (int i = 0; i < to_read; i++) {
                int c = input_get(t);
                if (c < 0) break;
                buf[actual++] = (char)c;
            }
            pthread_mutex_unlock(&t->input_lock);

            msg->regs.data[0] = (uint32_t)actual;
            msg->buffer.data = (uint64_t)(uintptr_t)buf;
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

        int ret = term_handle_msg(t, &msg);
        bool noreply = (msg.flags & ABI_IPC_FLAG_NOREPLY) != 0;
        if (ret == 0 && !noreply)
            sys_ipc_reply(&msg);
        /* ret == 1: 延迟回复 (pending_read) */
    }
    return NULL;
}

/* ============== 终端初始化 ============== */

static void term_init(struct terminal *t, int id, handle_t term_ep,
                      handle_t input_ep, handle_t output_ep) {
    memset(t, 0, sizeof(*t));
    t->id        = id;
    t->term_ep   = term_ep;
    t->input_ep  = input_ep;
    t->output_ep = output_ep;

    /* 默认 raw + echo (shell 自己做行编辑) */
    t->mode = LDISC_RAW;
    t->echo = 0;

    pthread_mutex_init(&t->input_lock, NULL);
}

/* ============== 入口 ============== */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* termd 自身的 stdio 走 debug fallback, 避免循环 */
    _stdio_set_fd(stdout, -1);
    _stdio_set_fd(stderr, -1);

    env_set_name("termd");
    handle_t serial_ep = env_get_handle("serial");
    handle_t kbd_ep    = env_get_handle("kbd_ep");

    /* tty1 (serial 终端): input=serial, output=serial */
    if (serial_ep != HANDLE_INVALID) {
        handle_t tty1_ep = env_get_handle(ABI_TTY1_HANDLE_NAME);
        if (tty1_ep == HANDLE_INVALID)
            tty1_ep = sys_endpoint_create(ABI_TTY1_HANDLE_NAME);

        if (tty1_ep != HANDLE_INVALID) {
            term_init(&g_terms[g_term_count], 1, tty1_ep, serial_ep, serial_ep);
            g_term_count++;
        }
    }

    /* tty0 (VGA 终端): 需要 displaydev (fbcond) + inputdev (kbd) 协议支持
       Phase 5 重写 kbd/fbcond 后启用. 当前只创建 endpoint 供 init 依赖解析 */
    (void)kbd_ep;
    handle_t tty0_ep = env_get_handle(ABI_TTY0_HANDLE_NAME);
    if (tty0_ep == HANDLE_INVALID)
        sys_endpoint_create(ABI_TTY0_HANDLE_NAME);

    /* 启动各终端的输入线程 + 服务线程 */
    for (int i = 0; i < g_term_count; i++) {
        pthread_t tid;
        pthread_create(&tid, NULL, input_thread, &g_terms[i]);

        pthread_t stid;
        pthread_create(&stid, NULL, service_thread, &g_terms[i]);
    }

    svc_notify_ready("termd");

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[termd]",
              " ready (%d terminals)\n", g_term_count);

    /* 主线程保持进程存活 */
    while (1) {
        msleep(10000);
    }

    return 0;
}
