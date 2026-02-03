/**
 * @file console.c
 * @brief 控制台 IPC 客户端
 *
 * 提供与 seriald/kbd 驱动的 IPC 通信封装.
 */

#include <stdio.h>
#include <xnix/ipc.h>
#include <xnix/ipc/console.h>
#include <xnix/syscall.h>

/* Console driver endpoints (boot时分配) */
#define CONSOLE_OUT_EP 0 /* seriald */
#define CONSOLE_IN_EP  3 /* kbd */

/**
 * 从控制台读取字符 (阻塞)
 */
int console_getc(void) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = CONSOLE_OP_GETC;

    /* 使用无限超时,等待用户输入 */
    int ret = sys_ipc_call(CONSOLE_IN_EP, &msg, &reply, 0);
    if (ret < 0) {
        return -1;
    }

    return (int)reply.regs.data[0];
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
