#ifndef VGAD_VGA_H
#define VGAD_VGA_H

#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA 16色调色板 */
#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW        14
#define VGA_COLOR_WHITE         15

struct vga_state {
    uint16_t *buffer; /* VGA 文本缓冲区位于 0xB8000 */
    int       cursor_x;
    int       cursor_y;
    uint8_t   attr; /* 当前属性 (前景色 | 背景色 << 4) */
};

void vga_state_init(struct vga_state *st);
void vga_hw_init(void);
void vga_putc(struct vga_state *st, char c);
void vga_write(struct vga_state *st, const char *data, int len);
void vga_set_color(struct vga_state *st, uint8_t fg, uint8_t bg);
void vga_reset_color(struct vga_state *st);
void vga_clear(struct vga_state *st);

#endif
