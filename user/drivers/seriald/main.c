/**
 * @file main.c
 * @brief Seriald UDM 驱动入口
 */

#include "serial.h"

#include <pthread.h>
#include <stdio.h>
#include <udm/server.h>
#include <unistd.h>
#include <xnix/syscall.h>
#include <xnix/udm/console.h>

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
 * 轮询串口输入,写入内核输入队列
 */
static void *input_thread(void *arg) {
    (void)arg;

    while (1) {
        int c = serial_getc();
        if (c >= 0) {
            /* 处理回车 -> 换行 */
            if (c == '\r') {
                c = '\n';
            }
            sys_input_write((char)c);
        } else {
            /* 无数据,短暂休眠 */
            msleep(10);
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
