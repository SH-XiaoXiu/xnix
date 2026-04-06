#include "../display/vga.h"

#include <xnix/displaydev.h>
#include <xnix/drvframework.h>
#include <xnix/protocol/displaydev.h>

#include <font/font.h>
#include <font/utf8.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

enum display_mode {
    DISPLAY_MODE_FB,
    DISPLAY_MODE_VGA,
};

struct fb_display_state {
    uint8_t *fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;
    uint8_t  bytes_per_pixel;

    uint8_t red_pos, red_size;
    uint8_t green_pos, green_size;
    uint8_t blue_pos, blue_size;

    int cols, rows;
    int cursor_x, cursor_y;

    uint8_t  cur_fg, cur_bg;
    uint32_t fg_color, bg_color;
};

struct displayd_state {
    enum display_mode mode;
    pthread_mutex_t   lock;
    union {
        struct fb_display_state fb;
        struct vga_state        vga;
    } u;
};

static struct displayd_state g_display;

static uint32_t fb_make_color(struct fb_display_state *st, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0;
    if (st->red_size)   color |= ((uint32_t)r >> (8u - st->red_size)) << st->red_pos;
    if (st->green_size) color |= ((uint32_t)g >> (8u - st->green_size)) << st->green_pos;
    if (st->blue_size)  color |= ((uint32_t)b >> (8u - st->blue_size)) << st->blue_pos;
    return color;
}

static void fb_putpixel(struct fb_display_state *st, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= st->fb_width || (uint32_t)y >= st->fb_height) {
        return;
    }
    uint8_t *p = st->fb_addr + (uint32_t)y * st->fb_pitch + (uint32_t)x * st->bytes_per_pixel;
    if (st->bytes_per_pixel == 4) {
        *(uint32_t *)p = color;
    } else if (st->bytes_per_pixel == 3) {
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
    }
}

static void fb_fill_rect(struct fb_display_state *st, int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)st->fb_width)  w = (int)st->fb_width - x;
    if (y + h > (int)st->fb_height) h = (int)st->fb_height - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        uint8_t *line = st->fb_addr + (uint32_t)(y + row) * st->fb_pitch
                        + (uint32_t)x * st->bytes_per_pixel;
        if (st->bytes_per_pixel == 4) {
            uint32_t *p = (uint32_t *)line;
            for (int col = 0; col < w; col++) p[col] = color;
        } else if (st->bytes_per_pixel == 3) {
            uint8_t b0 = (uint8_t)(color & 0xFFu);
            uint8_t b1 = (uint8_t)((color >> 8) & 0xFFu);
            uint8_t b2 = (uint8_t)((color >> 16) & 0xFFu);
            for (int col = 0; col < w; col++) {
                line[col * 3] = b0;
                line[col * 3 + 1] = b1;
                line[col * 3 + 2] = b2;
            }
        }
    }
}

static void fb_scroll_chars(struct fb_display_state *st, int lines) {
    int scroll_pixels = lines * CHAR_HEIGHT;
    if (scroll_pixels >= (int)st->fb_height) {
        fb_fill_rect(st, 0, 0, (int)st->fb_width, (int)st->fb_height, st->bg_color);
        st->cursor_x = 0;
        st->cursor_y = 0;
        return;
    }
    {
        uint32_t move_lines = st->fb_height - (uint32_t)scroll_pixels;
        memmove(st->fb_addr, st->fb_addr + (uint32_t)scroll_pixels * st->fb_pitch,
                move_lines * st->fb_pitch);
        fb_fill_rect(st, 0, (int)move_lines, (int)st->fb_width, scroll_pixels, st->bg_color);
    }
}

static void fb_draw_glyph_8x16(struct fb_display_state *st, int px, int py, const uint8_t *glyph) {
    for (int row = 0; row < CHAR_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < CHAR_WIDTH; col++) {
            uint32_t color = (bits & (1u << (7 - col))) ? st->fg_color : st->bg_color;
            fb_putpixel(st, px + col, py + row, color);
        }
    }
}

static void fb_apply_color(struct fb_display_state *st, uint8_t fg, uint8_t bg) {
    static const uint8_t palette[16][3] = {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
        {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
        {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
        {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
    };
    st->cur_fg = fg & 0x0Fu;
    st->cur_bg = bg & 0x0Fu;
    st->fg_color = fb_make_color(st, palette[st->cur_fg][0], palette[st->cur_fg][1], palette[st->cur_fg][2]);
    st->bg_color = fb_make_color(st, palette[st->cur_bg][0], palette[st->cur_bg][1], palette[st->cur_bg][2]);
}

static int fb_detect_last_row(struct fb_display_state *st) {
    for (int row = st->rows - 1; row >= 0; row--) {
        int py_start = row * CHAR_HEIGHT;
        int py_end = py_start + CHAR_HEIGHT;
        if ((uint32_t)py_end > st->fb_height) py_end = (int)st->fb_height;
        for (int y = py_start; y < py_end; y++) {
            uint8_t *line = st->fb_addr + (uint32_t)y * st->fb_pitch;
            uint32_t check = st->fb_width * st->bytes_per_pixel;
            for (uint32_t i = 0; i < check; i++) {
                if (line[i] != 0) return row;
            }
        }
    }
    return -1;
}

static void fb_newline(struct fb_display_state *st) {
    st->cursor_x = 0;
    st->cursor_y++;
    if (st->cursor_y >= st->rows) {
        fb_scroll_chars(st, 1);
        st->cursor_y = st->rows - 1;
    }
}

static void fb_putc(struct fb_display_state *st, uint32_t cp) {
    if (cp == '\n') { fb_newline(st); return; }
    if (cp == '\r') { st->cursor_x = 0; return; }
    if (cp == '\t') {
        int next = (st->cursor_x + 8) & ~7;
        if (next >= st->cols) fb_newline(st); else st->cursor_x = next;
        return;
    }
    if (cp == '\b') {
        if (st->cursor_x > 0) {
            st->cursor_x--;
            fb_fill_rect(st, st->cursor_x * CHAR_WIDTH, st->cursor_y * CHAR_HEIGHT,
                         CHAR_WIDTH, CHAR_HEIGHT, st->bg_color);
        }
        return;
    }
    if (st->cursor_x >= st->cols) fb_newline(st);

    {
        const uint8_t *glyph = font_get_ascii_8x16(cp);
        if (!glyph) glyph = font_get_ascii_8x16((uint32_t)'?');
        if (glyph) {
            fb_draw_glyph_8x16(st, st->cursor_x * CHAR_WIDTH, st->cursor_y * CHAR_HEIGHT, glyph);
        }
    }

    st->cursor_x++;
    if (st->cursor_x >= st->cols) fb_newline(st);
}

static void fb_write_bytes(struct fb_display_state *st, const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t cp = 0;
        size_t n = utf8_decode_next(data + i, len - i, &cp);
        if (n == 0) break;
        i += n;
        if (cp > 0x7Fu) cp = (uint32_t)'?';
        fb_putc(st, cp);
    }
}

static int displayd_write(struct display_device *dev, const void *buf, size_t len) {
    (void)dev;
    pthread_mutex_lock(&g_display.lock);
    if (g_display.mode == DISPLAY_MODE_FB) {
        fb_write_bytes(&g_display.u.fb, (const uint8_t *)buf, len);
    } else {
        vga_write(&g_display.u.vga, (const char *)buf, (int)len);
    }
    pthread_mutex_unlock(&g_display.lock);
    return (int)len;
}

static int displayd_clear(struct display_device *dev) {
    (void)dev;
    pthread_mutex_lock(&g_display.lock);
    if (g_display.mode == DISPLAY_MODE_FB) {
        fb_fill_rect(&g_display.u.fb, 0, 0, (int)g_display.u.fb.fb_width, (int)g_display.u.fb.fb_height,
                     g_display.u.fb.bg_color);
        g_display.u.fb.cursor_x = 0;
        g_display.u.fb.cursor_y = 0;
    } else {
        vga_clear(&g_display.u.vga);
    }
    pthread_mutex_unlock(&g_display.lock);
    return 0;
}

static int displayd_scroll(struct display_device *dev, int lines) {
    (void)dev;
    pthread_mutex_lock(&g_display.lock);
    if (g_display.mode == DISPLAY_MODE_FB) {
        fb_scroll_chars(&g_display.u.fb, lines);
    } else {
        vga_scroll_lines(&g_display.u.vga, lines);
    }
    pthread_mutex_unlock(&g_display.lock);
    return 0;
}

static int displayd_set_cursor(struct display_device *dev, int row, int col) {
    (void)dev;
    pthread_mutex_lock(&g_display.lock);
    if (g_display.mode == DISPLAY_MODE_FB) {
        g_display.u.fb.cursor_y = row;
        g_display.u.fb.cursor_x = col;
    } else {
        vga_set_cursor_pos(&g_display.u.vga, col, row);
    }
    pthread_mutex_unlock(&g_display.lock);
    return 0;
}

static int displayd_set_color(struct display_device *dev, uint8_t attr) {
    (void)dev;
    pthread_mutex_lock(&g_display.lock);
    if (g_display.mode == DISPLAY_MODE_FB) {
        fb_apply_color(&g_display.u.fb, attr & 0x0Fu, (attr >> 4) & 0x0Fu);
    } else {
        vga_set_color(&g_display.u.vga, attr & 0x0Fu, (attr >> 4) & 0x0Fu);
    }
    pthread_mutex_unlock(&g_display.lock);
    return 0;
}

static int displayd_reset_color(struct display_device *dev) {
    (void)dev;
    pthread_mutex_lock(&g_display.lock);
    if (g_display.mode == DISPLAY_MODE_FB) {
        fb_apply_color(&g_display.u.fb, 7, 0);
    } else {
        vga_reset_color(&g_display.u.vga);
    }
    pthread_mutex_unlock(&g_display.lock);
    return 0;
}

static struct display_ops g_display_ops = {
    .write       = displayd_write,
    .clear       = displayd_clear,
    .scroll      = displayd_scroll,
    .set_cursor  = displayd_set_cursor,
    .set_color   = displayd_set_color,
    .reset_color = displayd_reset_color,
};

static enum display_mode parse_mode(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode=vga") == 0) {
            return DISPLAY_MODE_VGA;
        }
        if (strcmp(argv[i], "--mode=fb") == 0) {
            return DISPLAY_MODE_FB;
        }
    }
    return DISPLAY_MODE_FB;
}

static int init_fb_mode(void) {
    handle_t fb_handle = env_get_handle("fb_mem");
    struct physmem_info pinfo;
    uint32_t mapped_size = 0;
    uint8_t *fb_addr;

    if (fb_handle == HANDLE_INVALID) {
        ulog_errf("[display] fb_mem missing\n");
        return -1;
    }
    if (sys_physmem_info(fb_handle, &pinfo) < 0 || pinfo.type != 1) {
        return -1;
    }
    if (pinfo.bpp != 24 && pinfo.bpp != 32) {
        return -1;
    }

    fb_addr = (uint8_t *)sys_mmap_phys(fb_handle, 0, 0, 0x03, &mapped_size);
    if (!fb_addr || (intptr_t)fb_addr < 0) {
        return -1;
    }

    memset(&g_display.u.fb, 0, sizeof(g_display.u.fb));
    g_display.u.fb.fb_addr         = fb_addr;
    g_display.u.fb.fb_width        = pinfo.width;
    g_display.u.fb.fb_height       = pinfo.height;
    g_display.u.fb.fb_pitch        = pinfo.pitch;
    g_display.u.fb.fb_bpp          = pinfo.bpp;
    g_display.u.fb.bytes_per_pixel = (uint8_t)(pinfo.bpp / 8);
    g_display.u.fb.red_pos         = pinfo.red_pos;
    g_display.u.fb.red_size        = pinfo.red_size;
    g_display.u.fb.green_pos       = pinfo.green_pos;
    g_display.u.fb.green_size      = pinfo.green_size;
    g_display.u.fb.blue_pos        = pinfo.blue_pos;
    g_display.u.fb.blue_size       = pinfo.blue_size;
    g_display.u.fb.cols            = (int)(g_display.u.fb.fb_width / CHAR_WIDTH);
    g_display.u.fb.rows            = (int)(g_display.u.fb.fb_height / CHAR_HEIGHT);
    fb_apply_color(&g_display.u.fb, 7, 0);
    g_display.u.fb.cursor_x = 0;
    g_display.u.fb.cursor_y = 0;
    return 0;
}

static int init_vga_mode(void) {
    void *addr = env_mmap_resource("vga_mem", NULL);
    if (!addr) {
        ulog_errf("[display] vga_mem missing\n");
        return -1;
    }
    vga_state_init(&g_display.u.vga);
    g_display.u.vga.buffer = (uint16_t *)addr;
    vga_hw_init();
    vga_clear(&g_display.u.vga);
    return 0;
}

int main(int argc, char **argv) {
    struct display_device dev;
    handle_t ep;

    env_set_name("display");
    memset(&g_display, 0, sizeof(g_display));
    pthread_mutex_init(&g_display.lock, NULL);
    g_display.mode = parse_mode(argc, argv);

    if (g_display.mode == DISPLAY_MODE_FB) {
        if (init_fb_mode() < 0) {
            return 1;
        }
        ep = env_get_handle("fbcon_ep");
        dev.name = "display-fb";
        dev.instance = 0;
        dev.caps = DISPDEV_CAP_COLOR | DISPDEV_CAP_CURSOR | DISPDEV_CAP_SCROLL;
        dev.rows = g_display.u.fb.rows;
        dev.cols = g_display.u.fb.cols;
    } else {
        if (init_vga_mode() < 0) {
            return 1;
        }
        ep = env_get_handle("vga_ep");
        dev.name = "display-vga";
        dev.instance = 0;
        dev.caps = DISPDEV_CAP_COLOR | DISPDEV_CAP_CURSOR | DISPDEV_CAP_SCROLL;
        dev.rows = VGA_HEIGHT;
        dev.cols = VGA_WIDTH;
    }

    if (ep == HANDLE_INVALID) {
        ulog_errf("[display] endpoint missing\n");
        return 1;
    }

    dev.ops = &g_display_ops;
    dev.endpoint = ep;

    if (displaydev_register(&dev) < 0) {
        ulog_errf("[display] register failed\n");
        return 1;
    }

    svc_notify_ready("display");
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[display]",
              " ready (%s)\n", g_display.mode == DISPLAY_MODE_FB ? "fb" : "vga");
    driver_run();
    return 0;
}
