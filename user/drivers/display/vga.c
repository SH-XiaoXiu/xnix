#include "vga.h"

#include <xnix/syscall.h>

#define VGA_CTRL_PORT 0x3D4
#define VGA_DATA_PORT 0x3D5

static void vga_update_cursor(struct vga_state *st) {
    uint16_t pos = st->cursor_y * VGA_WIDTH + st->cursor_x;
    sys_ioport_outb(VGA_CTRL_PORT, 0x0E);
    sys_ioport_outb(VGA_DATA_PORT, (pos >> 8) & 0xFF);
    sys_ioport_outb(VGA_CTRL_PORT, 0x0F);
    sys_ioport_outb(VGA_DATA_PORT, pos & 0xFF);
}

static void vga_scroll(struct vga_state *st) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            st->buffer[y * VGA_WIDTH + x] = st->buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    for (int x = 0; x < VGA_WIDTH; x++) {
        st->buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (uint16_t)' ' | ((uint16_t)st->attr << 8);
    }

    st->cursor_y = VGA_HEIGHT - 1;
}

void vga_scroll_lines(struct vga_state *st, int lines) {
    while (lines-- > 0) {
        vga_scroll(st);
    }
    vga_update_cursor(st);
}

void vga_state_init(struct vga_state *st) {
    st->buffer   = NULL;
    st->cursor_x = 0;
    st->cursor_y = 0;
    st->attr     = VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4);
}

void vga_hw_init(void) {
    /* cursor start: scan line 14, bit5=0 (enabled) */
    sys_ioport_outb(VGA_CTRL_PORT, 0x0A);
    sys_ioport_outb(VGA_DATA_PORT, 14);
    /* cursor end: scan line 15 → underline cursor */
    sys_ioport_outb(VGA_CTRL_PORT, 0x0B);
    sys_ioport_outb(VGA_DATA_PORT, 15);
}

void vga_putc(struct vga_state *st, char c) {
    if (!st->buffer) {
        return;
    }

    switch (c) {
    case '\n':
        st->cursor_x = 0;
        st->cursor_y++;
        break;
    case '\r':
        st->cursor_x = 0;
        break;
    case '\t':
        st->cursor_x = (st->cursor_x + 8) & ~7;
        if (st->cursor_x >= VGA_WIDTH) {
            st->cursor_x = 0;
            st->cursor_y++;
        }
        break;
    case '\b':
        if (st->cursor_x > 0) {
            st->cursor_x--;
        }
        break;
    default:
        if (c >= 32 && c <= 126) {
            st->buffer[st->cursor_y * VGA_WIDTH + st->cursor_x] =
                (uint16_t)c | ((uint16_t)st->attr << 8);
            st->cursor_x++;
            if (st->cursor_x >= VGA_WIDTH) {
                st->cursor_x = 0;
                st->cursor_y++;
            }
        }
        break;
    }

    if (st->cursor_y >= VGA_HEIGHT) {
        vga_scroll(st);
    }

    vga_update_cursor(st);
}

void vga_write(struct vga_state *st, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        vga_putc(st, data[i]);
    }
}

void vga_set_color(struct vga_state *st, uint8_t fg, uint8_t bg) {
    st->attr = (fg & 0x0F) | ((bg & 0x0F) << 4);
}

void vga_reset_color(struct vga_state *st) {
    st->attr = VGA_COLOR_LIGHT_GREY | (VGA_COLOR_BLACK << 4);
}

void vga_clear(struct vga_state *st) {
    if (!st->buffer) {
        return;
    }

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        st->buffer[i] = (uint16_t)' ' | ((uint16_t)st->attr << 8);
    }

    st->cursor_x = 0;
    st->cursor_y = 0;
    vga_update_cursor(st);
}

void vga_set_cursor_pos(struct vga_state *st, int x, int y) {
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= VGA_WIDTH) {
        x = VGA_WIDTH - 1;
    }
    if (y >= VGA_HEIGHT) {
        y = VGA_HEIGHT - 1;
    }
    st->cursor_x = x;
    st->cursor_y = y;
    vga_update_cursor(st);
}
