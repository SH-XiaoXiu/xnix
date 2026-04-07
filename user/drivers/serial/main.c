/**
 * @file main.c
 * @brief Seriald — 串口 chardev 驱动
 *
 * 通过 libdriver chardev 框架注册 COM1-4 为字符设备.
 * 消费者 (termd) 通过 CHARDEV_READ/WRITE 读写串口.
 *
 * 架构:
 *   chardev 服务线程 ←─ CHARDEV_WRITE ─── termd
 *                    ──→ serial_putc_port()
 *
 *   IRQ 监听线程 ──→ rx_buf ring buffer
 *   chardev 服务线程 ←─ CHARDEV_READ ──── termd
 *                    ──→ rx_buf 取数据
 */

#include "serial.h"

#include <xnix/chardev.h>
#include <xnix/drvframework.h>
#include <xnix/protocol/chardev.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* ============== 端口上下文 ============== */

#define RX_BUF_SIZE 256

struct port_context {
    uint16_t base;
    uint8_t  irq;
    int      index;
    bool     present;

    /* 输入 ring buffer */
    char              rx_buf[RX_BUF_SIZE];
    volatile uint8_t  rx_head;
    volatile uint8_t  rx_tail;
    handle_t          rx_notif;      /* notification: IRQ → read() 唤醒 */
    pthread_mutex_t   rx_lock;
    bool              last_was_cr;

    /* 输出锁 */
    pthread_mutex_t   tx_lock;

    struct char_device read_dev;   /* 读 endpoint (阻塞在 com_read) */
    struct char_device write_dev;  /* 写 endpoint (快速返回) */
};

struct irq_context {
    uint8_t irq;
    handle_t notif;
    int port_indices[MAX_COM_PORTS];
    int port_count;
};

static struct port_context g_ports[MAX_COM_PORTS];
static int                 g_port_count = 0;

static const uint16_t com_bases[MAX_COM_PORTS] = {
    COM1_BASE, COM2_BASE, COM3_BASE, COM4_BASE
};
static const uint8_t com_irqs[MAX_COM_PORTS] = {
    COM1_IRQ, COM2_IRQ, COM3_IRQ, COM4_IRQ
};

/* ============== ring buffer 操作 ============== */

static int rx_available(struct port_context *ctx) {
    if (ctx->rx_head >= ctx->rx_tail) {
        return ctx->rx_head - ctx->rx_tail;
    }
    return RX_BUF_SIZE - ctx->rx_tail + ctx->rx_head;
}

static void rx_put(struct port_context *ctx, char c) {
    uint8_t next = (ctx->rx_head + 1) % RX_BUF_SIZE;
    if (next == ctx->rx_tail) {
        return; /* 满，丢弃 */
    }
    ctx->rx_buf[ctx->rx_head] = c;
    ctx->rx_head = next;
}

static int rx_get(struct port_context *ctx) {
    if (ctx->rx_head == ctx->rx_tail) {
        return -1;
    }
    char c = ctx->rx_buf[ctx->rx_tail];
    ctx->rx_tail = (ctx->rx_tail + 1) % RX_BUF_SIZE;
    return (unsigned char)c;
}

/* ============== chardev ops ============== */

static int com_read(struct char_device *dev, void *buf, size_t max) {
    struct port_context *ctx = dev->priv;
    char *out = (char *)buf;
    int   count = 0;

    while (count == 0) {
        pthread_mutex_lock(&ctx->rx_lock);
        int avail = rx_available(ctx);
        if (avail > 0) {
            size_t to_read = (size_t)avail;
            if (to_read > max) {
                to_read = max;
            }
            for (size_t i = 0; i < to_read; i++) {
                int c = rx_get(ctx);
                if (c < 0) break;
                out[count++] = (char)c;
            }
        }
        pthread_mutex_unlock(&ctx->rx_lock);

        if (count == 0) {
            /* 等待 IRQ 线程唤醒 */
            sys_notification_wait(ctx->rx_notif);
        }
    }

    return count;
}

static int com_write(struct char_device *dev, const void *buf, size_t len) {
    struct port_context *ctx = dev->priv;
    const char *data = (const char *)buf;

    pthread_mutex_lock(&ctx->tx_lock);
    for (size_t i = 0; i < len; i++) {
        serial_putc_port(ctx->base, data[i]);
    }
    pthread_mutex_unlock(&ctx->tx_lock);

    return (int)len;
}

static struct char_ops com_read_ops = {
    .read = com_read,
};

static struct char_ops com_write_ops = {
    .write = com_write,
};

/* ============== IRQ 输入线程 ============== */

static void serial_consume_port(struct port_context *ctx) {
    pthread_mutex_lock(&ctx->rx_lock);
    for (;;) {
        int c = serial_getc_port(ctx->base);
        if (c < 0) {
            break;
        }
        if (ctx->last_was_cr && c == '\n') {
            ctx->last_was_cr = false;
            continue;
        }
        if (c == '\r') {
            c = '\n';
            ctx->last_was_cr = true;
        } else {
            ctx->last_was_cr = false;
        }
        rx_put(ctx, (char)c);
    }
    pthread_mutex_unlock(&ctx->rx_lock);
    sys_notification_signal(ctx->rx_notif, 1);
}

static void *irq_thread(void *arg) {
    struct irq_context *irq_ctx = (struct irq_context *)arg;

    int ret = sys_irq_bind(irq_ctx->irq, irq_ctx->notif, 1 << 0);
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[serial]",
                  " IRQ %d bind failed\n", irq_ctx->irq);
        return NULL;
    }

    while (1) {
        uint32_t bits = sys_notification_wait(irq_ctx->notif);
        if (bits == 0) {
            msleep(10);
            continue;
        }
        for (int i = 0; i < irq_ctx->port_count; i++) {
            serial_consume_port(&g_ports[irq_ctx->port_indices[i]]);
        }
    }

    return NULL;
}

/* ============== 入口 ============== */

int main(void) {
    struct irq_context irq_ctxs[2];
    int irq_ctx_count = 0;

    serial_hw_init(COM1_BASE);

    /* 探测 COM 端口 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        g_ports[i].base    = com_bases[i];
        g_ports[i].irq     = com_irqs[i];
        g_ports[i].index   = i;
        g_ports[i].present = false;
        g_ports[i].rx_head = 0;
        g_ports[i].rx_tail = 0;
        g_ports[i].write_dev = (struct char_device)CHAR_DEVICE_INIT;
        g_ports[i].read_dev  = (struct char_device)CHAR_DEVICE_INIT;
        g_ports[i].last_was_cr = false;
        pthread_mutex_init(&g_ports[i].rx_lock, NULL);
        pthread_mutex_init(&g_ports[i].tx_lock, NULL);

        if (serial_probe(com_bases[i])) {
            g_ports[i].present = true;
            if (i > 0) {
                serial_hw_init(com_bases[i]);
            }
            g_port_count++;
        }
    }

    ulog_tagf(stdout, TERM_COLOR_WHITE, "[serial]",
              " detected %d COM port(s)\n", g_port_count);

    /* 注册 chardev 设备 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present) {
            continue;
        }

        g_ports[i].rx_notif = sys_notification_create();

        /* 写 endpoint: COM1 使用 init 注入的 "serial" */
        if (i == 0) {
            handle_t ep = env_get_handle("serial");
            if (ep != HANDLE_INVALID)
                g_ports[i].write_dev.endpoint = ep;
        }
        g_ports[i].write_dev.name     = "com";
        g_ports[i].write_dev.instance = i;
        g_ports[i].write_dev.ops      = &com_write_ops;
        g_ports[i].write_dev.caps     = CHARDEV_CAP_WRITE;
        g_ports[i].write_dev.priv     = &g_ports[i];

        if (chardev_register(&g_ports[i].write_dev) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[serial]",
                      " COM%d: write register failed\n", i + 1);
            continue;
        }

        /* 读 endpoint: 独立 endpoint，阻塞读不影响写 */
        if (i == 0) {
            handle_t ep = env_get_handle("serial_in");
            if (ep != HANDLE_INVALID)
                g_ports[i].read_dev.endpoint = ep;
        }
        g_ports[i].read_dev.name     = "com_in";
        g_ports[i].read_dev.instance = i;
        g_ports[i].read_dev.ops      = &com_read_ops;
        g_ports[i].read_dev.caps     = CHARDEV_CAP_READ;
        g_ports[i].read_dev.priv     = &g_ports[i];

        if (chardev_register(&g_ports[i].read_dev) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[serial]",
                      " COM%d: read register failed\n", i + 1);
            continue;
        }

        ulog_tagf(stdout, TERM_COLOR_WHITE, "[serial]",
                  " COM%d (0x%x, IRQ %d) read+write registered\n",
                  i + 1, g_ports[i].base, g_ports[i].irq);
    }

    memset(irq_ctxs, 0, sizeof(irq_ctxs));
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present) {
            continue;
        }
        serial_enable_irq(g_ports[i].base);

        int ctx_index = -1;
        for (int j = 0; j < irq_ctx_count; j++) {
            if (irq_ctxs[j].irq == g_ports[i].irq) {
                ctx_index = j;
                break;
            }
        }
        if (ctx_index < 0) {
            ctx_index = irq_ctx_count++;
            irq_ctxs[ctx_index].irq = g_ports[i].irq;
            irq_ctxs[ctx_index].notif = sys_notification_create();
        }
        irq_ctxs[ctx_index].port_indices[irq_ctxs[ctx_index].port_count++] = i;
    }

    for (int i = 0; i < irq_ctx_count; i++) {
        if (irq_ctxs[i].notif == HANDLE_INVALID) {
            continue;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, irq_thread, &irq_ctxs[i]);
    }

    svc_notify_ready("serial");

    driver_run();

    return 0;
}
