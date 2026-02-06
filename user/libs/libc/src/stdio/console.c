/**
 * @file console.c
 * @brief 控制台 IPC 客户端
 *
 * 提供与 seriald/kbd 驱动的 IPC 通信封装.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/ipc.h>
#include <xnix/ipc/console.h>
#include <xnix/syscall.h>

/* 缓存的 handle(懒加载) */
static handle_t g_serial_ep = HANDLE_INVALID;
static handle_t g_kbd_ep    = HANDLE_INVALID;

static handle_t get_serial_ep(void) {
    if (g_serial_ep == HANDLE_INVALID) {
        int ret = sys_handle_find("serial");
        /* Check for negative error codes */
        if (ret < 0) {
            return HANDLE_INVALID;
        }
        g_serial_ep = (handle_t)ret;
    }
    return g_serial_ep;
}

static handle_t get_kbd_ep(void) {
    if (g_kbd_ep == HANDLE_INVALID) {
        g_kbd_ep = sys_handle_find("kbd_ep");
    }
    return g_kbd_ep;
}

/**
 * 从控制台读取字符 (阻塞)
 *
 * 如果 kbd 驱动尚未就绪,会阻塞重试直到可用.
 */
int console_getc(void) {
    while (1) {
        handle_t kbd = get_kbd_ep();
        if (kbd == HANDLE_INVALID) {
            msleep(500);
            g_kbd_ep = HANDLE_INVALID; /* 重试时重新查找 */
            continue;
        }

        struct ipc_message msg   = {0};
        struct ipc_message reply = {0};

        msg.regs.data[0] = CONSOLE_OP_GETC;

        int ret = sys_ipc_call(kbd, &msg, &reply, 0);
        if (ret == 0) {
            return (int)reply.regs.data[0];
        }

        /* kbd 驱动未就绪,等待后重试 */
        g_kbd_ep = HANDLE_INVALID;
        msleep(500);
    }
}

/**
 * 向控制台输出字符
 */
int console_putc(char c) {
    handle_t serial = get_serial_ep();
    if (serial == HANDLE_INVALID) {
        return -1;
    }

    struct ipc_message msg = {0};

    msg.regs.data[0] = CONSOLE_OP_PUTC;
    msg.regs.data[1] = (uint32_t)(unsigned char)c;

    /* 使用 send 不等待回复 */
    sys_ipc_send(serial, &msg, 100);

    return (unsigned char)c;
}
