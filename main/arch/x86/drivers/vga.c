/**
 * @file vga.c
 * @brief x86 VGA 文本模式驱动
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <xnix/console.h>
#include <xnix/types.h>

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA 颜色 */
enum vga_color {
    VGA_BLACK       = 0,
    VGA_BLUE        = 1,
    VGA_GREEN       = 2,
    VGA_CYAN        = 3,
    VGA_RED         = 4,
    VGA_MAGENTA     = 5,
    VGA_BROWN       = 6,
    VGA_LIGHT_GREY  = 7,
    VGA_DARK_GREY   = 8,
    VGA_LIGHT_BLUE  = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN  = 11,
    VGA_LIGHT_RED   = 12,
    VGA_PINK        = 13,
    VGA_YELLOW      = 14,
    VGA_WHITE       = 15,
};

static uint16_t *vga_buffer;
static int       vga_x, vga_y;
static uint8_t   vga_attr;

static inline uint8_t make_attr(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static inline uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)c | ((uint16_t)attr << 8);
}

static void vga_scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', vga_attr);
    }
}

static void vga_init(void) {
    vga_buffer = (uint16_t *)VGA_BUFFER;
    vga_x      = 0;
    vga_y      = 0;
    vga_attr   = make_attr(VGA_LIGHT_GREY, VGA_BLACK);
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else {
        vga_buffer[vga_y * VGA_WIDTH + vga_x] = make_entry(c, vga_attr);
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

static void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s++);
    }
}

static void vga_set_color(kcolor_t color) {
    if (color >= 0 && color <= 15) {
        vga_attr = make_attr((uint8_t)color, VGA_BLACK);
    }
}

static void vga_reset_color(void) {
    vga_attr = make_attr(VGA_LIGHT_GREY, VGA_BLACK);
}

static void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = make_entry(' ', vga_attr);
    }
    vga_x = 0;
    vga_y = 0;
}

/* 导出驱动结构 */
static struct console vga_console = {
    .name        = "vga",
    .flags       = CONSOLE_SYNC,
    .init        = vga_init,
    .putc        = vga_putc,
    .puts        = vga_puts,
    .set_color   = vga_set_color,
    .reset_color = vga_reset_color,
    .clear       = vga_clear,
};

void vga_console_register(void) {
    console_register(&vga_console);
}
