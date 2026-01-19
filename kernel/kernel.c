//
// Xnix Kernel - Created by XiaoXiu on 1/19/2026.
//

#include <stdint.h>

// 串口输出 (COM1)
#define SERIAL_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init(void) {
    outb(SERIAL_PORT + 1, 0x00);    // 禁用中断
    outb(SERIAL_PORT + 3, 0x80);    // 启用DLAB
    outb(SERIAL_PORT + 0, 0x03);    // 波特率 38400
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);    // 8位, 无校验, 1停止位
    outb(SERIAL_PORT + 2, 0xC7);    // 启用FIFO
    outb(SERIAL_PORT + 4, 0x0B);    // 启用IRQ
}

void serial_putc(char c) {
    while ((inb(SERIAL_PORT + 5) & 0x20) == 0);  // 等待发送缓冲区空
    outb(SERIAL_PORT, c);
}

void serial_puts(const char* str) {
    while (*str) {
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str++);
    }
}

// VGA 文本模式 屏幕显示
#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE = 1, VGA_GREEN = 2, VGA_CYAN = 3,
    VGA_RED = 4, VGA_MAGENTA = 5, VGA_BROWN = 6, VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11, VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13,
    VGA_LIGHT_BROWN = 14, VGA_WHITE = 15,
};

static uint16_t* const vga_buffer = (uint16_t*)VGA_BUFFER;

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_clear(uint8_t color) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', color);
    }
}

void vga_puts(const char* str, int x, int y, uint8_t color) {
    int i = 0;
    while (str[i] != '\0') {
        vga_buffer[y * VGA_WIDTH + x + i] = vga_entry(str[i], color);
        i++;
    }
}

void kprintf(const char* str) {
    serial_puts(str);
}

// 内核入口点
void kernel_main(void) {
    // 初始化串口
    serial_init();

    // 清屏
    uint8_t color = vga_entry_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_clear(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));

    // VGA显示
    vga_puts("========================================", 20, 10, color);
    vga_puts("        Xnix Kernel Loaded!             ", 20, 11, color);
    vga_puts("    Welcome to OS Development!          ", 20, 12, color);
    vga_puts("========================================", 20, 13, color);

    // 串口输出
    kprintf("\n");
    kprintf("========================================\n");
    kprintf("        Xnix 内核已加载!\n");
    kprintf("    欢迎进入操作系统开发!\n");
    kprintf("========================================\n");
    kprintf("\n");
    kprintf("串口已初始化 (COM1)\n");
    kprintf("VGA文本模式已初始化\n");
    kprintf("内核正在运行...\n");

    while(1) {
        __asm__ volatile("hlt");
    }
}