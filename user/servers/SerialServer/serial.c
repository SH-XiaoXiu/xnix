/**
 * @file serial.c
 * @brief 串口硬件操作实现
 */

#include "serial.h"

#include <xnix/syscall.h>

#define COM1 0x3F8

#define REG_DATA        0
#define REG_INTR_ENABLE 1
#define REG_DIVISOR_LO  0
#define REG_DIVISOR_HI  1
#define REG_FIFO_CTRL   2
#define REG_LINE_CTRL   3
#define REG_MODEM_CTRL  4
#define REG_LINE_STATUS 5
#define LSR_DATA_READY  0x01
#define LSR_TX_EMPTY    0x20

static uint32_t g_io_cap;

void serial_init(uint32_t io_cap) {
    g_io_cap = io_cap;

    sys_ioport_outb(g_io_cap, COM1 + REG_INTR_ENABLE, 0x00);
    sys_ioport_outb(g_io_cap, COM1 + REG_LINE_CTRL, 0x80);
    sys_ioport_outb(g_io_cap, COM1 + REG_DIVISOR_LO, 0x03);
    sys_ioport_outb(g_io_cap, COM1 + REG_DIVISOR_HI, 0x00);
    sys_ioport_outb(g_io_cap, COM1 + REG_LINE_CTRL, 0x03);
    sys_ioport_outb(g_io_cap, COM1 + REG_FIFO_CTRL, 0xC7);
    sys_ioport_outb(g_io_cap, COM1 + REG_MODEM_CTRL, 0x0B);
}

void serial_enable_irq(void) {
    /* 开启接收数据就绪中断 */
    sys_ioport_outb(g_io_cap, COM1 + REG_INTR_ENABLE, 0x01);
}

void serial_putc(char c) {
    while (1) {
        int lsr = sys_ioport_inb(g_io_cap, COM1 + REG_LINE_STATUS);
        if (lsr >= 0 && (lsr & LSR_TX_EMPTY)) {
            break;
        }
    }
    sys_ioport_outb(g_io_cap, COM1 + REG_DATA, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (s && *s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

void serial_clear(void) {
    serial_puts("\033[2J\033[H");
}

int serial_data_available(void) {
    int lsr = sys_ioport_inb(g_io_cap, COM1 + REG_LINE_STATUS);
    return (lsr >= 0) && (lsr & LSR_DATA_READY);
}

int serial_getc(void) {
    if (!serial_data_available()) {
        return -1;
    }
    return sys_ioport_inb(g_io_cap, COM1 + REG_DATA);
}
