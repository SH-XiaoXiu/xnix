/**
 * @file console.c
 * @brief x86 控制台实现
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <arch/console.h>
#include <arch/cpu.h>
#include <arch/io.h>

#include <drivers/serial.h>
#include <drivers/vga.h>
#include <xstd/stdio.h>

#define X86_VGA_BUFFER 0xB8000

void arch_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint8_t arch_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void arch_console_init(void) {
    serial_init(SERIAL_COM1);
    vga_init((void *)X86_VGA_BUFFER);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void arch_putc(char c) {
    serial_putc(SERIAL_COM1, c);
    vga_putc(c);
}

void arch_halt(void) {
    __asm__ volatile("hlt");
}

/* kcolor_t -> ANSI 前景色码 */
static const char *ansi_color_codes[] = {
    "\033[30m", /* BLACK   */
    "\033[34m", /* BLUE    */
    "\033[32m", /* GREEN   */
    "\033[36m", /* CYAN    */
    "\033[31m", /* RED     */
    "\033[35m", /* MAGENTA */
    "\033[33m", /* BROWN   */
    "\033[37m", /* LGRAY   */
    "\033[90m", /* DGRAY   */
    "\033[94m", /* LBLUE   */
    "\033[92m", /* LGREEN  */
    "\033[96m", /* LCYAN   */
    "\033[91m", /* LRED    */
    "\033[95m", /* PINK    */
    "\033[93m", /* YELLOW  */
    "\033[97m", /* WHITE   */
};

void arch_set_color(kcolor_t color) {
    if (color < 0 || color > 15) {
        return;
    }
    /* VGA: 只改前景色，背景保持黑色 */
    vga_set_color((enum vga_color)color, VGA_BLACK);
    /* Serial: 输出 ANSI 转义码 */
    serial_puts(SERIAL_COM1, ansi_color_codes[color]);
}

void arch_reset_color(void) {
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    serial_puts(SERIAL_COM1, "\033[0m");
}
