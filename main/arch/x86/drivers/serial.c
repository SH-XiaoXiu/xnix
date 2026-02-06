/**
 * @file serial.c
 * @brief x86 串口驱动 (8250/16550) - 纯同步早期控制台
 *
 * 为 early_console 提供串口输出后端.
 * 所有输出同步直接写硬件,无 ring buffer,无消费者线程.
 */

#include <arch/cpu.h>

#include <asm/irq_defs.h>
#include <xnix/early_console.h>
#include <xnix/irq.h>
#include <xnix/sync.h>

/* 串口输出锁,保护多核同时输出 */
static spinlock_t serial_lock = SPINLOCK_INIT;

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
    uint32_t flags = spin_lock_irqsave(&serial_lock);
    if (c == '\n') {
        serial_putc_hw('\r');
    }
    serial_putc_hw(c);
    spin_unlock_irqrestore(&serial_lock, flags);
}

static void serial_puts_sync(const char *s) {
    uint32_t flags = spin_lock_irqsave(&serial_lock);
    while (*s) {
        if (*s == '\n') {
            serial_putc_hw('\r');
        }
        serial_putc_hw(*s++);
    }
    spin_unlock_irqrestore(&serial_lock, flags);
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
    .name = "serial",
    .init = serial_init,
    .putc = serial_putc_sync,
    .puts = serial_puts_sync,
};

void serial_console_register(void) {
    early_console_register(&serial_backend);
}
