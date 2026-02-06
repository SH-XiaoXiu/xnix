/**
 * @file serial.c
 * @brief x86 串口驱动 (8250/16550) - 纯同步早期控制台
 *
 * 为 early_console 提供串口输出后端.
 * 所有输出同步直接写硬件,无 ring buffer,无消费者线程.
 */

#include <arch/cpu.h>

#include <asm/irq_defs.h>
#include <drivers/serial_hw_lock.h>
#include <xnix/early_console.h>
#include <xnix/irq.h>
#include <xnix/stdio.h>

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

/* 直接写硬件 */
static void serial_putc_hw(char c) {
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_TX_EMPTY) == 0);
    outb(COM1 + REG_DATA, (uint8_t)c);
}

/* 同步输出(带锁保护,\n 前自动加 \r) */
static void serial_putc_sync(char c) {
    uint32_t flags = serial_hw_lock_irqsave();
    if (c == '\n') {
        serial_putc_hw('\r');
    }
    serial_putc_hw(c);
    serial_hw_unlock_irqrestore(flags);
}

static void serial_puts_sync(const char *s) {
    uint32_t flags = serial_hw_lock_irqsave();
    while (*s) {
        if (*s == '\n') {
            serial_putc_hw('\r');
        }
        serial_putc_hw(*s++);
    }
    serial_hw_unlock_irqrestore(flags);
}

static int serial_vga_color_to_ansi_fg(uint8_t color) {
    static const int map[16] = {
        30, /* black */
        34, /* blue */
        32, /* green */
        36, /* cyan */
        31, /* red */
        35, /* magenta */
        33, /* brown/yellow */
        37, /* light grey */
        90, /* dark grey */
        94, /* light blue */
        92, /* light green */
        96, /* light cyan */
        91, /* light red */
        95, /* light magenta */
        93, /* light brown/yellow */
        97, /* white */
    };
    return map[color & 0x0F];
}


static void serial_set_color(uint8_t fg, uint8_t bg) {
    char seq[32];
    int  fg_code = serial_vga_color_to_ansi_fg(fg);
    (void)bg;

    int n = snprintf(seq, sizeof(seq), "\x1b[%dm", fg_code);
    if (n > 0) {
        serial_puts_sync(seq);
    }
}

static void serial_reset_color(void) {
    serial_puts_sync("\x1b[0m");
}

#define IRQ_SERIAL 4

static void serial_irq_handler(struct irq_regs *frame) {
    (void)frame;
    if (inb(COM1 + REG_LINE_STATUS) & 0x01) {
        uint8_t data = inb(COM1 + REG_DATA);
        irq_user_push(IRQ_SERIAL, data);
    }
}

static void serial_init(void) {
    outb(COM1 + REG_INTR_ENABLE, 0x00);
    outb(COM1 + REG_LINE_CTRL, 0x80);
    outb(COM1 + REG_DIVISOR_LO, 0x03);
    outb(COM1 + REG_DIVISOR_HI, 0x00);
    outb(COM1 + REG_LINE_CTRL, 0x03);
    outb(COM1 + REG_FIFO_CTRL, 0xC7);
    outb(COM1 + REG_MODEM_CTRL, 0x0B);

    irq_set_handler(IRQ_SERIAL, serial_irq_handler);
}

static struct early_console_backend serial_backend = {
    .name        = "serial",
    .init        = serial_init,
    .putc        = serial_putc_sync,
    .puts        = serial_puts_sync,
    .set_color   = serial_set_color,
    .reset_color = serial_reset_color,
};

void serial_console_register(void) {
    early_console_register(&serial_backend);
}
