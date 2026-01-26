/**
 * @file console.c
 * @brief UDM 控制台 client stub
 *
 * 内核侧通过 IPC 将控制台输出发送给用户态 UDM 驱动.
 */

#include <xnix/capability.h>
#include <xnix/console.h>
#include <xnix/ipc.h>
#include <xnix/string.h>
#include <xnix/udm/console.h>

static cap_handle_t g_console_ep = CAP_HANDLE_INVALID;

/**
 * 设置 UDM 控制台 endpoint
 *
 * 在 seriald 等 UDM 驱动启动后调用,将内核输出切换到 IPC 模式.
 */
void udm_console_set_endpoint(cap_handle_t ep) {
    g_console_ep = ep;
}

/**
 * 获取 UDM 控制台 endpoint
 */
cap_handle_t udm_console_get_endpoint(void) {
    return g_console_ep;
}

static void udm_console_putc(char c) {
    if (g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_PUTC;
    msg.regs.data[1] = (uint32_t)(uint8_t)c;

    ipc_send_async(g_console_ep, &msg);
}

static void udm_console_puts(const char *s) {
    if (!s) {
        return;
    }
    while (*s) {
        udm_console_putc(*s++);
    }
}

static void udm_console_set_color(kcolor_t color) {
    if (g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_SET_COLOR;
    msg.regs.data[1] = (uint32_t)color;

    ipc_send_async(g_console_ep, &msg);
}

static void udm_console_reset_color(void) {
    if (g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_RESET_COLOR;

    ipc_send_async(g_console_ep, &msg);
}

static void udm_console_clear(void) {
    if (g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_CLEAR;

    ipc_send_async(g_console_ep, &msg);
}

static struct console udm_console_driver = {
    .name        = "udm",
    .init        = NULL,
    .putc        = udm_console_putc,
    .puts        = udm_console_puts,
    .set_color   = udm_console_set_color,
    .reset_color = udm_console_reset_color,
    .clear       = udm_console_clear,
};

/**
 * 获取 UDM 控制台驱动
 */
struct console *udm_console_get_driver(void) {
    return &udm_console_driver;
}
