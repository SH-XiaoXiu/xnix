/**
 * @file serial.c
 * @brief x86 串口驱动 (8250/16550) - 内核直驱
 *
 * 用于启动早期的内核日志输出.UDM 切换后此驱动不再使用.
 */

#include <arch/cpu.h>

#include <xnix/console.h>

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
