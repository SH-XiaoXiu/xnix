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
#include <unistd.h>
#include <xnix/abi/ipc.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

/* 保护串口硬件访问的互斥锁 */
static pthread_mutex_t serial_lock;

static int console_handler(struct ipc_message *msg) {
    uint32_t type = msg->regs.data[0];

    switch (type) {
    case SERIAL_MSG_WRITE: {
        /* 数据从 data[1] 开始,最多 ABI_IPC_MSG_PAYLOAD_BYTES */
        const char *str = (const char *)&msg->regs.data[1];
        /* 计算实际长度 */
        size_t max_len = ABI_IPC_MSG_PAYLOAD_BYTES;

        /* 加锁保护硬件访问 */
        pthread_mutex_lock(&serial_lock);
        for (size_t i = 0; i < max_len && str[i]; i++) {
            serial_putc(str[i]);
        }
        pthread_mutex_unlock(&serial_lock);
        break;
    }
    case SERIAL_MSG_COLOR: {
        /* 颜色设置 (保留,暂不实现) */
        (void)msg;
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
        serial_puts("[seriald] kbd_ep not found, input forwarding disabled\n");
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
    serial_puts("[seriald] main() entered\n");

    /* 初始化互斥锁 */
    pthread_mutex_init(&serial_lock, NULL);
    serial_puts("[seriald] mutex initialized\n");

    /* 使用 init 传递的 endpoint handle (seriald provides serial) */
    handle_t ep = env_get_handle("serial");
    if (ep == HANDLE_INVALID) {
        serial_puts("[seriald] ERROR: 'serial' handle not found\n");
        return 1;
    }
    serial_puts("[seriald] found serial endpoint handle\n");

    /* 启动输入处理线程 */
    serial_puts("[seriald] creating input thread\n");
    pthread_t tid;
    if (pthread_create(&tid, NULL, input_thread, NULL) != 0) {
        serial_puts("[seriald] ERROR: failed to create input thread\n");
    } else {
        serial_puts("[seriald] input thread created\n");
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = console_handler,
        .name     = "seriald",
    };

    serial_puts("[seriald] initializing UDM server\n");
    udm_server_init(&srv);
    serial_puts("[seriald] UDM server initialized\n");

    /* 通知 init 服务已就绪 */
    serial_puts("[seriald] notifying ready\n");
    svc_notify_ready("seriald");
    serial_puts("[seriald] ready notification sent\n");

    serial_puts("[seriald] entering server loop\n");
    udm_server_run(&srv);

    serial_puts("[seriald] ERROR: server loop exited\n");
    return 0;
}
