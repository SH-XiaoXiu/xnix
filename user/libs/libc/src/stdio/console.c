/**
 * @file console.c
 * @brief 控制台 IPC 客户端
 *
 * 提供与 seriald/kbd 驱动的 IPC 通信封装.
 */

#include <stdio.h>
#include <unistd.h>
#include <xnix/ipc.h>
#include <xnix/ipc/console.h>
#include <xnix/syscall.h>

/* Console driver endpoints (boot时分配) */
#define CONSOLE_OUT_EP 0 /* seriald */
#define CONSOLE_IN_EP  3 /* kbd */

/**
 * 从控制台读取字符 (阻塞)
 *
 * 如果 kbd 驱动尚未就绪,会阻塞重试直到可用.
 */
int console_getc(void) {
    while (1) {
        struct ipc_message msg   = {0};
        struct ipc_message reply = {0};

        msg.regs.data[0] = CONSOLE_OP_GETC;

        int ret = sys_ipc_call(CONSOLE_IN_EP, &msg, &reply, 0);
        if (ret == 0) {
            return (int)reply.regs.data[0];
        }

        /* kbd 驱动未就绪,等待后重试 */
        msleep(500);
    }
}

/**
 * 向控制台输出字符
 */
int console_putc(char c) {
    struct ipc_message msg = {0};

    msg.regs.data[0] = CONSOLE_OP_PUTC;
    msg.regs.data[1] = (uint32_t)c;

    /* 使用 send 不等待回复 */
    sys_ipc_send(CONSOLE_OUT_EP, &msg, 100);

    return (unsigned char)c;
}
