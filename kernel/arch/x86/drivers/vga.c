/**
 * @file vga.c
 * @brief x86 VGA 文本模式驱动实现
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <drivers/vga.h>

static uint16_t* vga_buffer;
static int vga_x, vga_y;
static uint8_t vga_color_attr;

static inline uint8_t make_color(enum vga_color fg, enum vga_color bg) {
    return fg | (bg << 4);
}

static inline uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_init(void* buffer) {
    vga_buffer = (uint16_t*)buffer;
    vga_x = 0;
    vga_y = 0;
    vga_color_attr = make_color(VGA_LIGHT_GREY, VGA_BLACK);
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    vga_color_attr = make_color(fg, bg);
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = make_entry(' ', vga_color_attr);
    }
    vga_x = 0;
    vga_y = 0;
}

static void vga_scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', vga_color_attr);
    }
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else {
        vga_buffer[vga_y * VGA_WIDTH + vga_x] = make_entry(c, vga_color_attr);
        vga_x++;
    }

    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }

    if (vga_y >= VGA_HEIGHT) {
        vga_scroll();
        vga_y = VGA_HEIGHT - 1;
    }
}

void vga_puts_at(const char* str, int x, int y) {
    int i = 0;
    while (str[i] && (x + i) < VGA_WIDTH) {
        vga_buffer[y * VGA_WIDTH + x + i] = make_entry(str[i], vga_color_attr);
        i++;
    }
}
