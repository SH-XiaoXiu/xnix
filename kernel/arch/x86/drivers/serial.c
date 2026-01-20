/**
 * @file serial.c
 * @brief x86 串口驱动实现（8250/16550）
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <drivers/serial.h>
#include <arch/io.h>

#define SERIAL_DATA          0
#define SERIAL_INTR_ENABLE   1
#define SERIAL_DIVISOR_LO    0
#define SERIAL_DIVISOR_HI    1
#define SERIAL_FIFO_CTRL     2
#define SERIAL_LINE_CTRL     3
#define SERIAL_MODEM_CTRL    4
#define SERIAL_LINE_STATUS   5
#define SERIAL_LSR_TX_EMPTY  0x20

void serial_init(uint16_t port) {
    arch_outb(port + SERIAL_INTR_ENABLE, 0x00);
    arch_outb(port + SERIAL_LINE_CTRL, 0x80);
    arch_outb(port + SERIAL_DIVISOR_LO, 0x03);
    arch_outb(port + SERIAL_DIVISOR_HI, 0x00);
    arch_outb(port + SERIAL_LINE_CTRL, 0x03);
    arch_outb(port + SERIAL_FIFO_CTRL, 0xC7);
    arch_outb(port + SERIAL_MODEM_CTRL, 0x0B);
}

void serial_putc(uint16_t port, char c) {
    while ((arch_inb(port + SERIAL_LINE_STATUS) & SERIAL_LSR_TX_EMPTY) == 0);
    arch_outb(port + SERIAL_DATA, (uint8_t)c);
}

void serial_puts(uint16_t port, const char* str) {
    while (*str) {
        if (*str == '\n') serial_putc(port, '\r');
        serial_putc(port, *str++);
    }
}
