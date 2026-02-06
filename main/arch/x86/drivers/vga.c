/**
 * @file vga.c
 * @brief x86 VGA 文本模式驱动 - 早期控制台后端
 *
 * 纯文本输出,无 ANSI 转义解析,无颜色.
 * 默认属性:浅灰前景 + 黑色背景.
 */

#include <arch/cpu.h>

#include <xnix/early_console.h>
#include <xnix/types.h>

/* 检查 framebuffer 信息是否存在(由 fb.c 提供) */
extern bool fb_info_available(void);

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA CRT 控制器端口 */
#define VGA_CRTC_INDEX   0x3D4
#define VGA_CRTC_DATA    0x3D5
#define VGA_CURSOR_HIGH  0x0E
#define VGA_CURSOR_LOW   0x0F
#define VGA_CURSOR_START 0x0A
#define VGA_CURSOR_END   0x0B

/* VGA 颜色 */
enum vga_color {
    VGA_BLACK      = 0,
    VGA_LIGHT_GREY = 7,
};

static uint16_t *vga_buffer;
static int       vga_x, vga_y;
static uint8_t   vga_attr;

static inline uint8_t make_attr(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void vga_update_cursor(void) {
    uint16_t pos = vga_y * VGA_WIDTH + vga_x;
    outb(VGA_CRTC_INDEX, VGA_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (pos >> 8) & 0xFF);
    outb(VGA_CRTC_INDEX, VGA_CURSOR_LOW);
    outb(VGA_CRTC_DATA, pos & 0xFF);
}

static void vga_enable_cursor(void) {
    outb(VGA_CRTC_INDEX, VGA_CURSOR_START);
    outb(VGA_CRTC_DATA, 14);
    outb(VGA_CRTC_INDEX, VGA_CURSOR_END);
    outb(VGA_CRTC_DATA, 15);
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
    vga_enable_cursor();
    vga_update_cursor();
}

static void vga_putc(char c) {
    if (fb_info_available()) {
        return;
    }

    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
        }
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
    vga_update_cursor();
}

static void vga_puts(const char *s) {
    if (fb_info_available()) {
        return;
    }
    while (*s) {
        vga_putc(*s++);
    }
}

static void vga_clear(void) {
    if (fb_info_available()) {
        return;
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = make_entry(' ', vga_attr);
    }
    vga_x = 0;
    vga_y = 0;
    vga_update_cursor();
}

static struct early_console_backend vga_backend = {
    .name  = "vga",
    .init  = vga_init,
    .putc  = vga_putc,
    .puts  = vga_puts,
    .clear = vga_clear,
};

void vga_console_register(void) {
    early_console_register(&vga_backend);
}
