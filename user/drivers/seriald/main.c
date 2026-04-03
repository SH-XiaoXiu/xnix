/**
 * @file main.c
 * @brief Seriald UDM 驱动 (多端口)
 *
 * 自动探测 COM1-COM4, 为每个端口创建独立 endpoint 和服务线程.
 * COM1 使用 init 注入的 "serial" endpoint (兼容已有架构).
 * COM2+ 动态创建 endpoint, 并通过 TTY_OP_CREATE 请求 ttyd 创建对应 tty.
 */

#include "serial.h"

#include <xnix/protocol/serial.h>
#include <xnix/protocol/tty.h>
#include <d/server.h>
#include <libs/serial/serial.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/ipc.h>
#include <xnix/abi/syscall.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* 每个 COM 端口的状态 */
struct com_port {
    uint16_t        base;       /* IO 基地址 */
    uint8_t         irq;        /* IRQ 号 */
    int             index;      /* 端口序号 (0=COM1, 1=COM2, ...) */
    bool            present;    /* 探测到硬件 */
    handle_t        endpoint;   /* IPC 服务 endpoint */
    handle_t        tty_ep;     /* 对应的 tty endpoint (由 ttyd 创建) */
    pthread_mutex_t lock;       /* 保护硬件访问 */
};

static struct com_port g_ports[MAX_COM_PORTS];
static int             g_port_count = 0;

static const uint16_t com_bases[MAX_COM_PORTS] = {
    COM1_BASE, COM2_BASE, COM3_BASE, COM4_BASE
};
static const uint8_t com_irqs[MAX_COM_PORTS] = {
    COM1_IRQ, COM2_IRQ, COM3_IRQ, COM4_IRQ
};

/* ============== 串口输出 ============== */

static void port_write_bytes(struct com_port *port, const char *buf, size_t len) {
    if (!buf || len == 0) {
        return;
    }
    pthread_mutex_lock(&port->lock);
    for (size_t i = 0; i < len; i++) {
        serial_putc_port(port->base, buf[i]);
    }
    pthread_mutex_unlock(&port->lock);
}

static void port_write_cstr(struct com_port *port, const char *s, size_t max_len) {
    if (!s || max_len == 0) {
        return;
    }
    pthread_mutex_lock(&port->lock);
    for (size_t i = 0; i < max_len && s[i]; i++) {
        serial_putc_port(port->base, s[i]);
    }
    pthread_mutex_unlock(&port->lock);
}

/* ============== ANSI 颜色映射 ============== */

static int vga_color_to_ansi_fg(uint8_t color) {
    static const int map[16] = {
        30, 34, 32, 36, 31, 35, 33, 37,
        90, 94, 92, 96, 91, 95, 93, 97,
    };
    return map[color & 0x0F];
}

static void port_apply_color(struct com_port *port, uint8_t attr) {
    char seq[32];
    int  n = snprintf(seq, sizeof(seq), "\x1b[%dm", vga_color_to_ansi_fg(attr & 0x0F));
    if (n > 0) {
        port_write_bytes(port, seq, (size_t)n);
    }
}

/* ============== 消息处理 ============== */

static int console_handler_for_port(struct com_port *port, struct ipc_message *msg) {
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
            port_write_cstr(port, str, ABI_IPC_MSG_PAYLOAD_BYTES);
        } else {
            port_write_bytes(port, &c, 1);
        }
        break;
    }
    case UDM_CONSOLE_SET_COLOR:
        port_apply_color(port, (uint8_t)(msg->regs.data[1] & 0xFF));
        break;
    case UDM_CONSOLE_RESET_COLOR:
        port_write_bytes(port, "\x1b[0m", 4);
        break;
    case UDM_CONSOLE_CLEAR:
        port_write_bytes(port, "\x1b[2J\x1b[H", 7);
        break;
    case UDM_CONSOLE_WRITE: {
        const char *data = (const char *)&msg->regs.data[1];
        size_t      len  = (size_t)(msg->regs.data[7] & 0xFF);
        if (len > UDM_CONSOLE_WRITE_MAX) {
            len = UDM_CONSOLE_WRITE_MAX;
        }
        port_write_bytes(port, data, len);
        break;
    }
    default:
        break;
    }
    return 0;
}

/* ============== 每端口服务线程 ============== */

static void *port_service_thread(void *arg) {
    struct com_port *port = (struct com_port *)arg;

    while (1) {
        struct ipc_message msg = {0};
        int ret = sys_ipc_receive(port->endpoint, &msg, 0);
        if (ret < 0) {
            msleep(10);
            continue;
        }

        console_handler_for_port(port, &msg);
        sys_ipc_reply(&msg);
    }

    return NULL;
}

/* ============== 每端口输入线程 ============== */

struct input_thread_args {
    struct com_port *port;
};

static void *input_thread(void *arg) {
    struct com_port *port = (struct com_port *)arg;

    /* 等待 tty endpoint 可用 */
    handle_t tty_ep = port->tty_ep;
    if (tty_ep == HANDLE_INVALID) {
        /* COM1 的 tty_ep 需要等待 ttyd 启动 */
        for (int retry = 0; retry < 50; retry++) {
            tty_ep = env_get_handle("tty1");
            if (tty_ep != HANDLE_INVALID) {
                break;
            }
            msleep(100);
        }
        if (tty_ep == HANDLE_INVALID) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[seriald]",
                      " COM%d: tty not found, input disabled\n", port->index + 1);
            return NULL;
        }
        port->tty_ep = tty_ep;
    }

    /* 创建 notification 用于接收中断 */
    handle_t notif = sys_notification_create();
    if (notif == HANDLE_INVALID) {
        return NULL;
    }

    /* TODO: 共享 IRQ 当前不支持 (COM1/COM3 共用 IRQ4, COM2/COM4 共用 IRQ3),
     *       同一 IRQ 被多个端口 bind 时会失败. 需要内核支持 shared IRQ. */
    int ret = sys_irq_bind(port->irq, notif, 1 << 0);
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[seriald]",
                  " COM%d: IRQ %d bind failed (shared IRQ not supported)\n",
                  port->index + 1, port->irq);
        return NULL;
    }

    serial_enable_irq(port->base);

    bool last_was_cr = false;

    while (1) {
        uint32_t bits = sys_notification_wait(notif);
        if (bits == 0) {
            msleep(10);
            continue;
        }

        uint8_t buf[128];
        int     n = sys_irq_read(port->irq, buf, sizeof(buf), 0);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                int c = buf[i];
                if (last_was_cr && c == '\n') {
                    last_was_cr = false;
                    continue;
                }
                if (c == '\r') {
                    c           = '\n';
                    last_was_cr = true;
                } else {
                    last_was_cr = false;
                }

                struct ipc_message msg = {0};
                msg.regs.data[0]       = TTY_OP_INPUT;
                msg.regs.data[1]       = (uint32_t)c;
                sys_ipc_send(tty_ep, &msg, 0);
            }
        }
    }

    return NULL;
}

/* ============== 动态 TTY 创建 ============== */

/**
 * 通过 TTY_OP_CREATE 请求 ttyd 为指定串口创建 TTY
 * @param port  要绑定的串口
 * @param tty1_ep 已有的 tty1 endpoint (用于发送 CREATE 请求)
 */
static void create_tty_for_port(struct com_port *port, handle_t tty1_ep) {
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0]      = TTY_OP_CREATE;
    req.handles.handles[0] = port->endpoint; /* output → 此端口的 serial endpoint */
    req.handles.count      = 1;

    int ret = sys_ipc_call(tty1_ep, &req, &reply, 2000);
    if (ret < 0 || (int32_t)reply.regs.data[0] != 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[seriald]",
                  " COM%d: TTY_OP_CREATE failed\n", port->index + 1);
        return;
    }

    int      tty_id = (int)reply.regs.data[1];
    handle_t new_ep = (reply.handles.count > 0) ? reply.handles.handles[0] : HANDLE_INVALID;
    port->tty_ep = new_ep;

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[seriald]",
              " COM%d → tty%d created\n", port->index + 1, tty_id);
}

/* ============== 入口 ============== */

int main(void) {
    serial_hw_init(COM1_BASE); /* COM1 先初始化 (启动输出依赖它) */

    env_set_name("seriald");

    /* 探测 COM 端口 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        g_ports[i].base    = com_bases[i];
        g_ports[i].irq     = com_irqs[i];
        g_ports[i].index   = i;
        g_ports[i].present = false;
        g_ports[i].endpoint = HANDLE_INVALID;
        g_ports[i].tty_ep   = HANDLE_INVALID;
        pthread_mutex_init(&g_ports[i].lock, NULL);

        if (serial_probe(com_bases[i])) {
            g_ports[i].present = true;
            if (i > 0) {
                serial_hw_init(com_bases[i]);
            }
            g_port_count++;
        }
    }

    ulog_tagf(stdout, TERM_COLOR_WHITE, "[seriald]",
              " detected %d COM port(s)\n", g_port_count);

    /* COM1 使用 init 注入的 "serial" endpoint */
    g_ports[0].endpoint = env_require("serial");
    if (g_ports[0].endpoint == HANDLE_INVALID) {
        return 1;
    }

    /* COM2+ 创建独立 endpoint */
    for (int i = 1; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present) {
            continue;
        }
        char ep_name[16];
        snprintf(ep_name, sizeof(ep_name), "serial%d", i);
        g_ports[i].endpoint = sys_endpoint_create(ep_name);
        if (g_ports[i].endpoint == HANDLE_INVALID) {
            g_ports[i].present = false;
            continue;
        }
        ulog_tagf(stdout, TERM_COLOR_WHITE, "[seriald]",
                  " COM%d (0x%x, IRQ %d) → endpoint %s\n",
                  i + 1, g_ports[i].base, g_ports[i].irq, ep_name);
    }

    /* 为 COM2+ 启动服务线程 */
    for (int i = 1; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present || g_ports[i].endpoint == HANDLE_INVALID) {
            continue;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, port_service_thread, &g_ports[i]);
    }

    /* 通知就绪 (COM1 可用即就绪) */
    svc_notify_ready("seriald");

    /* 等待 ttyd 就绪, 然后为 COM2+ 创建 TTY */
    handle_t tty1_ep = HANDLE_INVALID;
    for (int retry = 0; retry < 100; retry++) {
        tty1_ep = env_get_handle("tty1");
        if (tty1_ep != HANDLE_INVALID) {
            break;
        }
        msleep(100);
    }
    if (tty1_ep != HANDLE_INVALID) {
        for (int i = 1; i < MAX_COM_PORTS; i++) {
            if (!g_ports[i].present || g_ports[i].endpoint == HANDLE_INVALID) {
                continue;
            }
            create_tty_for_port(&g_ports[i], tty1_ep);
        }
    }

    /* 为每个端口启动输入线程 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present || g_ports[i].endpoint == HANDLE_INVALID) {
            continue;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, input_thread, &g_ports[i]);
    }

    /* 主线程: COM1 服务循环 */
    struct udm_server srv = {
        .endpoint = g_ports[0].endpoint,
        .handler  = (int (*)(struct ipc_message *))NULL,
        .name     = "seriald",
    };

    /* 使用手动循环替代 udm_server_run (需要传递 port 参数) */
    while (1) {
        struct ipc_message msg = {0};
        int ret = sys_ipc_receive(g_ports[0].endpoint, &msg, 0);
        if (ret < 0) {
            msleep(10);
            continue;
        }

        console_handler_for_port(&g_ports[0], &msg);
        sys_ipc_reply(&msg);
    }

    return 0;
}
