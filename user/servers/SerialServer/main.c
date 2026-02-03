/**
 * @file main.c
 * @brief Seriald UDM 驱动入口
 */

#include "serial.h"

#include <d/protocol/serial.h>
#include <d/server.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <xnix/ipc.h>
#include <xnix/ipc/console.h>
#include <xnix/syscall.h>

#define BOOT_CONSOLE_EP 0
#define BOOT_IOPORT_CAP 1

static int console_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    switch (op) {
    case UDM_CONSOLE_PUTC:
        serial_putc(UDM_MSG_ARG(msg, 0) & 0xFF);
        break;
    case UDM_CONSOLE_WRITE: {
        /* 字符串从 data[1] 开始,最多 24 字节 */
        const char *str = (const char *)&msg->regs.data[1];
        for (uint32_t i = 0; i < UDM_CONSOLE_WRITE_MAX && str[i]; i++) {
            serial_putc(str[i]);
        }
        break;
    }
    /* SET_COLOR 和 RESET_COLOR 不再使用,颜色通过 ANSI 序列在文本中传递 */
    case UDM_CONSOLE_CLEAR:
        serial_clear();
        break;
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
    const cap_handle_t kbd_ep      = 3;
    bool               last_was_cr = false;

    /* 创建 notification 用于接收中断通知 */
    cap_handle_t notif = sys_notification_create();
    if (notif == CAP_HANDLE_INVALID) {
        printf("[seriald] failed to create notification\n");
        return NULL;
    }

    /* 绑定 IRQ 4 (COM1) 到 notification 的第 0 位 */
    int ret = sys_irq_bind(4, notif, 1 << 0);
    if (ret < 0) {
        printf("[seriald] failed to bind IRQ 4: %d\n", ret);
        return NULL;
    }

    /* 开启硬件中断 */
    serial_enable_irq();

    printf("[seriald] IRQ-driven input thread started (IRQ 4 -> handle %u)\n", notif);

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
    serial_init(BOOT_IOPORT_CAP);

    /* 启动输入处理线程 */
    pthread_t tid;
    if (pthread_create(&tid, NULL, input_thread, NULL) != 0) {
        printf("[seriald] failed to create input thread\n");
    }

    struct udm_server srv = {
        .endpoint = BOOT_CONSOLE_EP,
        .handler  = console_handler,
        .name     = "seriald",
    };

    udm_server_init(&srv);
    udm_server_run(&srv);

    return 0;
}
