/**
 * @file serial.c
 * @brief 串口硬件操作实现 (多端口)
 */

#include "serial.h"

#include <xnix/syscall.h>

#define REG_DATA        0
#define REG_INTR_ENABLE 1
#define REG_DIVISOR_LO  0
#define REG_DIVISOR_HI  1
#define REG_FIFO_CTRL   2
#define REG_LINE_CTRL   3
#define REG_MODEM_CTRL  4
#define REG_LINE_STATUS 5
#define REG_SCRATCH     7
#define LSR_DATA_READY  0x01
#define LSR_TX_EMPTY    0x20

int serial_probe(uint16_t port) {
    /* 写 scratch register 然后读回,匹配则端口存在 */
    sys_ioport_outb(port + REG_SCRATCH, 0xAA);
    int val = sys_ioport_inb(port + REG_SCRATCH);
    if (val != 0xAA) {
        return 0;
    }
    /* 再检查 LSR 不是全 1 (无硬件时读回 0xFF) */
    int lsr = sys_ioport_inb(port + REG_LINE_STATUS);
    if (lsr == 0xFF) {
        return 0;
    }
    return 1;
}

void serial_hw_init(uint16_t port) {
    sys_ioport_outb(port + REG_INTR_ENABLE, 0x00);
    sys_ioport_outb(port + REG_LINE_CTRL, 0x80);   /* DLAB on */
    sys_ioport_outb(port + REG_DIVISOR_LO, 0x03);  /* 38400 baud */
    sys_ioport_outb(port + REG_DIVISOR_HI, 0x00);
    sys_ioport_outb(port + REG_LINE_CTRL, 0x03);   /* 8N1 */
    sys_ioport_outb(port + REG_FIFO_CTRL, 0xC7);   /* FIFO */
    sys_ioport_outb(port + REG_MODEM_CTRL, 0x0B);  /* DTR + RTS + OUT2 */
}

void serial_enable_irq(uint16_t port) {
    sys_ioport_outb(port + REG_INTR_ENABLE, 0x01);
}

void serial_putc_port(uint16_t port, char c) {
    if (c == '\n') {
        serial_putc_port(port, '\r');
    }
    while (1) {
        int lsr = sys_ioport_inb(port + REG_LINE_STATUS);
        if (lsr >= 0 && (lsr & LSR_TX_EMPTY)) {
            break;
        }
    }
    sys_ioport_outb(port + REG_DATA, (uint8_t)c);
}

void serial_puts_port(uint16_t port, const char *s) {
    while (s && *s) {
        serial_putc_port(port, *s++);
    }
}

void serial_clear_port(uint16_t port) {
    serial_puts_port(port, "\033[2J\033[H");
}

int serial_data_available(uint16_t port) {
    int lsr = sys_ioport_inb(port + REG_LINE_STATUS);
    return (lsr >= 0) && (lsr & LSR_DATA_READY);
}

int serial_getc_port(uint16_t port) {
    if (!serial_data_available(port)) {
        return -1;
    }
    return sys_ioport_inb(port + REG_DATA);
}
