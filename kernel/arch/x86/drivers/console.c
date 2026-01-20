/**
 * @file console.c
 * @brief x86 控制台实现
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <arch/console.h>
#include <arch/io.h>
#include <arch/cpu.h>
#include <drivers/serial.h>
#include <drivers/vga.h>

#define X86_VGA_BUFFER 0xB8000

void arch_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t arch_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void arch_console_init(void) {
    serial_init(SERIAL_COM1);
    vga_init((void*)X86_VGA_BUFFER);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void arch_putc(char c) {
    serial_putc(SERIAL_COM1, c);
    vga_putc(c);
}

void arch_halt(void) {
    __asm__ volatile ("hlt");
}
