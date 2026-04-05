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

    /* 输出锁 */
    pthread_mutex_t   tx_lock;

    struct char_device dev;
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

static struct char_ops com_ops = {
    .read  = com_read,
    .write = com_write,
};

/* ============== IRQ 输入线程 ============== */

static void *irq_thread(void *arg) {
    struct port_context *ctx = (struct port_context *)arg;

    handle_t irq_notif = sys_notification_create();
    if (irq_notif == HANDLE_INVALID) {
        return NULL;
    }

    int ret = sys_irq_bind(ctx->irq, irq_notif, 1 << 0);
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[seriald]",
                  " COM%d: IRQ %d bind failed\n", ctx->index + 1, ctx->irq);
        return NULL;
    }

    serial_enable_irq(ctx->base);

    bool last_was_cr = false;

    while (1) {
        uint32_t bits = sys_notification_wait(irq_notif);
        if (bits == 0) {
            msleep(10);
            continue;
        }

        uint8_t buf[128];
        int n = sys_irq_read(ctx->irq, buf, sizeof(buf), 0);
        if (n <= 0) {
            continue;
        }

        pthread_mutex_lock(&ctx->rx_lock);
        for (int i = 0; i < n; i++) {
            int c = buf[i];
            /* \r\n → \n 去重 */
            if (last_was_cr && c == '\n') {
                last_was_cr = false;
                continue;
            }
            if (c == '\r') {
                c = '\n';
                last_was_cr = true;
            } else {
                last_was_cr = false;
            }
            rx_put(ctx, (char)c);
        }
        pthread_mutex_unlock(&ctx->rx_lock);

        /* 唤醒阻塞在 read() 的 chardev 服务线程 */
        sys_notification_signal(ctx->rx_notif, 1);
    }

    return NULL;
}

/* ============== 入口 ============== */

int main(void) {
    serial_hw_init(COM1_BASE);

    env_set_name("seriald");

    /* 探测 COM 端口 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        g_ports[i].base    = com_bases[i];
        g_ports[i].irq     = com_irqs[i];
        g_ports[i].index   = i;
        g_ports[i].present = false;
        g_ports[i].rx_head = 0;
        g_ports[i].rx_tail = 0;
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

    ulog_tagf(stdout, TERM_COLOR_WHITE, "[seriald]",
              " detected %d COM port(s)\n", g_port_count);

    /* 注册 chardev 设备 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present) {
            continue;
        }

        /* 创建输入唤醒 notification */
        g_ports[i].rx_notif = sys_notification_create();

        /* COM1 的端点由 init 注入为 "serial" */
        if (i == 0) {
            handle_t ep = env_get_handle("serial");
            if (ep != HANDLE_INVALID) {
                g_ports[i].dev.endpoint = ep;
            }
        }

        g_ports[i].dev.name     = "com";
        g_ports[i].dev.instance = i;
        g_ports[i].dev.ops      = &com_ops;
        g_ports[i].dev.caps     = CHARDEV_CAP_READ | CHARDEV_CAP_WRITE;
        g_ports[i].dev.priv     = &g_ports[i];

        if (chardev_register(&g_ports[i].dev) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[seriald]",
                      " COM%d: register failed\n", i + 1);
            continue;
        }

        ulog_tagf(stdout, TERM_COLOR_WHITE, "[seriald]",
                  " COM%d (0x%x, IRQ %d) registered\n",
                  i + 1, g_ports[i].base, g_ports[i].irq);
    }

    /* 启动 IRQ 输入线程 */
    for (int i = 0; i < MAX_COM_PORTS; i++) {
        if (!g_ports[i].present) {
            continue;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, irq_thread, &g_ports[i]);
    }

    svc_notify_ready("seriald");

    driver_run();

    return 0;
}
