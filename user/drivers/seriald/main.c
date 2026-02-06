/**
 * @file main.c
 * @brief Seriald UDM 驱动入口
 *
 * 使用新的IPC消息格式 (SERIAL_MSG_WRITE, SERIAL_MSG_COLOR)
 */

#include "serial.h"

#include <d/protocol/serial.h>
#include <d/server.h>
#include <libs/serial/serial.h> /* 消息类型定义 */
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <xnix/abi/ipc.h>
#include <xnix/abi/syscall.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

/* 保护串口硬件访问的互斥锁 */
static pthread_mutex_t serial_lock;

static void debug_write(const char *s) {
    if (!s) {
        return;
    }

    uint32_t len = 0;
    while (s[len] && len < 512) {
        len++;
    }
    if (len == 0) {
        return;
    }

    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(SYS_DEBUG_WRITE), "b"((uint32_t)(uintptr_t)s), "c"(len)
                 : "memory");
    (void)ret;
}

static void serial_write_bytes(const char *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }

    pthread_mutex_lock(&serial_lock);
    for (size_t i = 0; i < len; i++) {
        serial_putc(buf[i]);
    }
    pthread_mutex_unlock(&serial_lock);
}

static void serial_write_cstr(const char *s, size_t max_len) {
    if (!s || max_len == 0) {
        return;
    }

    pthread_mutex_lock(&serial_lock);
    for (size_t i = 0; i < max_len && s[i]; i++) {
        serial_putc(s[i]);
    }
    pthread_mutex_unlock(&serial_lock);
}

static int vga_color_to_ansi_fg(uint8_t color) {
    static const int map[16] = {
        30, /* black */
        34, /* blue */
        32, /* green */
        36, /* cyan */
        31, /* red */
        35, /* magenta */
        33, /* brown/yellow */
        37, /* light grey */
        90, /* dark grey */
        94, /* light blue */
        92, /* light green */
        96, /* light cyan */
        91, /* light red */
        95, /* light magenta */
        93, /* light brown/yellow */
        97, /* white */
    };
    return map[color & 0x0F];
}


static void serial_apply_color_attr(uint8_t attr) {
    uint8_t fg = attr & 0x0F;

    char seq[32];
    int  fg_code = vga_color_to_ansi_fg(fg);
    int  n       = snprintf(seq, sizeof(seq), "\x1b[%dm", fg_code);
    if (n > 0) {
        serial_write_bytes(seq, (size_t)n);
    }
}

static int console_handler(struct ipc_message *msg) {
    uint32_t type = msg->regs.data[0];

    switch (type) {
    case UDM_CONSOLE_PUTC: {
        uint32_t   v = msg->regs.data[1];
        const char c = (char)(v & 0xFF);

        const uint8_t *bytes           = (const uint8_t *)&msg->regs.data[1];
        bool           looks_like_cstr = (bytes[1] != 0) || (bytes[2] != 0) || (bytes[3] != 0) ||
                               (msg->regs.data[2] != 0) || (msg->regs.data[3] != 0) ||
                               (msg->regs.data[4] != 0) || (msg->regs.data[5] != 0) ||
                               (msg->regs.data[6] != 0) || (msg->regs.data[7] != 0);

        if (looks_like_cstr) {
            const char *str = (const char *)&msg->regs.data[1];
            serial_write_cstr(str, ABI_IPC_MSG_PAYLOAD_BYTES);
        } else {
            serial_write_bytes(&c, 1);
        }
        break;
    }
    case UDM_CONSOLE_SET_COLOR: {
        uint8_t attr = (uint8_t)(msg->regs.data[1] & 0xFF);
        serial_apply_color_attr(attr);
        break;
    }
    case UDM_CONSOLE_RESET_COLOR: {
        serial_write_bytes("\x1b[0m", 4);
        break;
    }
    case UDM_CONSOLE_CLEAR: {
        serial_write_bytes("\x1b[2J\x1b[H", 7);
        break;
    }
    case UDM_CONSOLE_WRITE: {
        const char *data = (const char *)&msg->regs.data[1];
        size_t      len  = (size_t)(msg->regs.data[7] & 0xFF);
        if (len > UDM_CONSOLE_WRITE_MAX) {
            len = UDM_CONSOLE_WRITE_MAX;
        }
        serial_write_bytes(data, len);
        break;
    }
    default:
        break;
    }
    return 0;
}

/**
 * 输入处理线程
 * 使用中断驱动,读取串口输入并发送给 kbd 驱动
 */
static void *input_thread(void *arg) {
    (void)arg;

    /* kbd 驱动的 endpoint */
    const handle_t kbd_ep = env_get_handle("kbd_ep");
    if (kbd_ep == HANDLE_INVALID) {
        debug_write("[seriald] kbd_ep not found, input forwarding disabled\n");
        return NULL;
    }
    bool last_was_cr = false;

    /* 创建 notification 用于接收中断通知 */
    handle_t notif = sys_notification_create();
    if (notif == HANDLE_INVALID) {
        return NULL;
    }

    /* 绑定 IRQ 4 (COM1) 到 notification 的第 0 位 */
    int ret = sys_irq_bind(4, notif, 1 << 0);
    if (ret < 0) {
        return NULL;
    }

    /* 开启硬件中断 */
    serial_enable_irq();

    while (1) {
        /* 等待中断通知 */
        uint32_t bits = sys_notification_wait(notif);
        if (bits == 0) {
            /* 如果 wait 返回 0 且 handle 有效,可能是被中断或其它原因,短暂休眠避免忙等 */
            msleep(10);
            continue;
        }

        /* 中断触发后,从内核 IRQ 缓冲区读取数据 */
        uint8_t buf[128];
        int     n = sys_irq_read(4, buf, sizeof(buf), 0); /* 非阻塞读取 */
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                int c = buf[i];
                if (last_was_cr && c == '\n') {
                    last_was_cr = false;
                    continue;
                }
                /* 处理回车 -> 换行 */
                if (c == '\r') {
                    c           = '\n';
                    last_was_cr = true;
                } else {
                    last_was_cr = false;
                }

                /* 通过 IPC 发送字符给 kbd */
                struct ipc_message msg = {0};
                msg.regs.data[0]       = 1; /* CONSOLE_OP_PUTC */
                msg.regs.data[1]       = (uint32_t)c;

                sys_ipc_send(kbd_ep, &msg, 0);
            }
        }
    }

    return NULL;
}

int main(void) {
    /* 初始化串口硬件 (直接访问硬件,基于权限) */
    serial_hw_init();

    /* Early output to confirm we're running */
    debug_write("[seriald] main() entered\n");

    /* 初始化互斥锁 */
    pthread_mutex_init(&serial_lock, NULL);
    debug_write("[seriald] mutex initialized\n");

    /* 使用 init 传递的 endpoint handle (seriald provides serial) */
    handle_t ep = env_get_handle("serial");
    if (ep == HANDLE_INVALID) {
        debug_write("[seriald] ERROR: 'serial' handle not found\n");
        return 1;
    }
    debug_write("[seriald] found serial endpoint handle\n");

    /* 启动输入处理线程 */
    debug_write("[seriald] creating input thread\n");
    pthread_t tid;
    if (pthread_create(&tid, NULL, input_thread, NULL) != 0) {
        debug_write("[seriald] ERROR: failed to create input thread\n");
    } else {
        debug_write("[seriald] input thread created\n");
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = console_handler,
        .name     = "seriald",
    };

    udm_server_init(&srv);
    debug_write("[seriald] server initialized\n");

    /* 通知 init 服务已就绪 */
    debug_write("[seriald] notifying ready\n");
    svc_notify_ready("seriald");

    udm_server_run(&srv);

    debug_write("[seriald] ERROR: server loop exited\n");
    return 0;
}
