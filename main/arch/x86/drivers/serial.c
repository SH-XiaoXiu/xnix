/**
 * @file serial.c
 * @brief x86 串口驱动 (8250/16550)
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <arch/cpu.h>

#include <xnix/console.h>
#include <xnix/ipc.h>
#include <xnix/thread.h>
#include <xnix/string.h>

#define COM1 0x3F8

#define REG_DATA        0
#define REG_INTR_ENABLE 1
#define REG_DIVISOR_LO  0
#define REG_DIVISOR_HI  1
#define REG_FIFO_CTRL   2
#define REG_LINE_CTRL   3
#define REG_MODEM_CTRL  4
#define REG_LINE_STATUS 5
#define LSR_TX_EMPTY    0x20

/* ANSI 颜色码 */
static const char *ansi_colors[] = {
    "\033[30m", "\033[34m", "\033[32m", "\033[36m", "\033[31m", "\033[35m", "\033[33m", "\033[37m",
    "\033[90m", "\033[94m", "\033[92m", "\033[96m", "\033[91m", "\033[95m", "\033[93m", "\033[97m",
};

static void serial_init(void) {
    outb(COM1 + REG_INTR_ENABLE, 0x00);
    outb(COM1 + REG_LINE_CTRL, 0x80);
    outb(COM1 + REG_DIVISOR_LO, 0x03);
    outb(COM1 + REG_DIVISOR_HI, 0x00);
    outb(COM1 + REG_LINE_CTRL, 0x03);
    outb(COM1 + REG_FIFO_CTRL, 0xC7);
    outb(COM1 + REG_MODEM_CTRL, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_TX_EMPTY) == 0);
    outb(COM1 + REG_DATA, (uint8_t)c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

static void serial_set_color(kcolor_t color) {
    if (color >= 0 && color <= 15) {
        serial_puts(ansi_colors[color]);
    }
}

static void serial_reset_color(void) {
    serial_puts("\033[0m");
}

static void serial_clear(void) {
    serial_puts("\033[2J\033[H");
}

static cap_handle_t serial_udm_ep = CAP_HANDLE_INVALID;

static void serial_udm_server(void *arg) {
    cap_handle_t ep = (cap_handle_t)(uintptr_t)arg;

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));

    while (1) {
        msg.buffer.data = NULL;
        msg.buffer.size = 0;
        ipc_receive(ep, &msg, 0);

        uint32_t op = msg.regs.data[0];
        switch (op) {
        case CONSOLE_UDM_OP_PUTC:
            serial_putc((char)(msg.regs.data[1] & 0xFF));
            break;
        case CONSOLE_UDM_OP_SET_COLOR:
            serial_set_color((kcolor_t)msg.regs.data[1]);
            break;
        case CONSOLE_UDM_OP_RESET_COLOR:
            serial_reset_color();
            break;
        case CONSOLE_UDM_OP_CLEAR:
            serial_clear();
            break;
        default:
            break;
        }
    }
}

static void serial_udm_putc(char c) {
    if (serial_udm_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = CONSOLE_UDM_OP_PUTC;
    msg.regs.data[1] = (uint32_t)(uint8_t)c;

    /* 发送到消息队列,如果队列满则回退到直接输出 */
    if (ipc_send_async(serial_udm_ep, &msg) != IPC_OK) {
        serial_putc(c);
    }
}

static void serial_udm_puts(const char *s) {
    if (!s) {
        return;
    }
    while (*s) {
        if (*s == '\n') {
            serial_udm_putc('\r');
        }
        serial_udm_putc(*s++);
    }
}

static void serial_udm_set_color(kcolor_t color) {
    if (serial_udm_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = CONSOLE_UDM_OP_SET_COLOR;
    msg.regs.data[1] = (uint32_t)color;
    ipc_send_async(serial_udm_ep, &msg);
}

static void serial_udm_reset_color(void) {
    if (serial_udm_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = CONSOLE_UDM_OP_RESET_COLOR;
    ipc_send_async(serial_udm_ep, &msg);
}

static void serial_udm_clear(void) {
    if (serial_udm_ep == CAP_HANDLE_INVALID) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = CONSOLE_UDM_OP_CLEAR;
    ipc_send_async(serial_udm_ep, &msg);
}

static struct console serial_console_udm = {
    .name        = "serial",
    .init        = NULL,
    .putc        = serial_udm_putc,
    .puts        = serial_udm_puts,
    .set_color   = serial_udm_set_color,
    .reset_color = serial_udm_reset_color,
    .clear       = serial_udm_clear,
};

/* 导出驱动结构 */
static struct console serial_console = {
    .name        = "serial",
    .init        = serial_init,
    .putc        = serial_putc,
    .puts        = serial_puts,
    .set_color   = serial_set_color,
    .reset_color = serial_reset_color,
    .clear       = serial_clear,
};

void serial_console_register(void) {
    console_register(&serial_console);
}

cap_handle_t serial_udm_start(void) {
    if (serial_udm_ep != CAP_HANDLE_INVALID) {
        return serial_udm_ep;
    }

    serial_udm_ep = endpoint_create();
    if (serial_udm_ep == CAP_HANDLE_INVALID) {
        return CAP_HANDLE_INVALID;
    }

    thread_t t = thread_create("seriald", serial_udm_server, (void *)(uintptr_t)serial_udm_ep);
    if (!t) {
        cap_close(serial_udm_ep);
        serial_udm_ep = CAP_HANDLE_INVALID;
        return CAP_HANDLE_INVALID;
    }

    return serial_udm_ep;
}

struct console *serial_udm_console_driver(void) {
    return &serial_console_udm;
}
