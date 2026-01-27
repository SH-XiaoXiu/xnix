/**
 * @file console.c
 * @brief UDM 控制台 client stub
 *
 * 内核侧通过 IPC 将控制台输出发送给用户态 UDM 驱动.
 * 使用行缓冲减少 IPC 开销.
 */

#include <xnix/capability.h>
#include <xnix/console.h>
#include <xnix/ipc.h>
#include <xnix/string.h>
#include <xnix/udm/console.h>

static cap_handle_t g_console_ep = CAP_HANDLE_INVALID;

/* 行缓冲区 */
static char g_line_buf[UDM_CONSOLE_WRITE_MAX];
static int  g_line_len = 0;

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

/* 刷新缓冲区,批量发送 */
static void udm_console_flush(void) {
    if (g_line_len == 0 || g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_WRITE;

    /* 将字符串打包到 data[1..7] */
    char *dst = (char *)&msg.regs.data[1];
    for (int i = 0; i < g_line_len; i++) {
        dst[i] = g_line_buf[i];
    }
    dst[g_line_len] = '\0';

    ipc_send_async(g_console_ep, &msg);
    g_line_len = 0;
}

static void udm_console_putc(char c) {
    if (g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    /* 添加到缓冲区 */
    g_line_buf[g_line_len++] = c;

    /* 遇到换行或缓冲区满时刷新 */
    if (c == '\n' || g_line_len >= UDM_CONSOLE_WRITE_MAX - 1) {
        udm_console_flush();
    }
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

    /* 颜色变化前先刷新缓冲区 */
    udm_console_flush();

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

    /* 颜色变化前先刷新缓冲区 */
    udm_console_flush();

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = UDM_CONSOLE_RESET_COLOR;

    ipc_send_async(g_console_ep, &msg);
}

static void udm_console_clear(void) {
    if (g_console_ep == CAP_HANDLE_INVALID) {
        return;
    }

    /* 清屏前先刷新缓冲区 */
    udm_console_flush();

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
