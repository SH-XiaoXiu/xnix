#include <d/protocol/serial.h>
#include <d/server.h>
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

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

struct fbcon_state {
    uint8_t *fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint8_t  fb_bpp;
    uint8_t  bytes_per_pixel;

    uint8_t red_pos;
    uint8_t red_size;
    uint8_t green_pos;
    uint8_t green_size;
    uint8_t blue_pos;
    uint8_t blue_size;

    int cols;
    int rows;
    int cursor_x;
    int cursor_y;

    uint8_t  cur_fg;
    uint8_t  cur_bg;
    uint32_t fg_color;
    uint32_t bg_color;

    pthread_mutex_t lock;
};

static struct fbcon_state g_fbcon;

static uint32_t fb_make_color(struct fbcon_state *st, uint8_t r, uint8_t g, uint8_t b) {
    if (!st) {
        return 0;
    }
    uint32_t color = 0;
    if (st->red_size) {
        color |= ((uint32_t)r >> (8u - st->red_size)) << st->red_pos;
    }
    if (st->green_size) {
        color |= ((uint32_t)g >> (8u - st->green_size)) << st->green_pos;
    }
    if (st->blue_size) {
        color |= ((uint32_t)b >> (8u - st->blue_size)) << st->blue_pos;
    }
    return color;
}

static void fb_putpixel(struct fbcon_state *st, int x, int y, uint32_t color) {
    if (!st || !st->fb_addr) {
        return;
    }
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

static void fb_fill_rect(struct fbcon_state *st, int x, int y, int w, int h, uint32_t color) {
    if (!st || !st->fb_addr || w <= 0 || h <= 0) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)st->fb_width) {
        w = (int)st->fb_width - x;
    }
    if (y + h > (int)st->fb_height) {
        h = (int)st->fb_height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    if (st->bytes_per_pixel == 4) {
        for (int row = 0; row < h; row++) {
            uint32_t *line = (uint32_t *)(st->fb_addr + (uint32_t)(y + row) * st->fb_pitch) + x;
            for (int col = 0; col < w; col++) {
                line[col] = color;
            }
        }
    } else if (st->bytes_per_pixel == 3) {
        uint8_t b0 = (uint8_t)(color & 0xFFu);
        uint8_t b1 = (uint8_t)((color >> 8) & 0xFFu);
        uint8_t b2 = (uint8_t)((color >> 16) & 0xFFu);
        for (int row = 0; row < h; row++) {
            uint8_t *line = st->fb_addr + (uint32_t)(y + row) * st->fb_pitch + (uint32_t)x * 3u;
            for (int col = 0; col < w; col++) {
                line[col * 3 + 0] = b0;
                line[col * 3 + 1] = b1;
                line[col * 3 + 2] = b2;
            }
        }
    }
}

/**
 * 扫描 framebuffer,找到最后一行有内容的字符行
 *
 * 用于 fbcond 初始化时继承内核早期控制台的光标位置.
 * 从底部向上扫描,找到第一个有非黑色像素的字符行.
 *
 * @return 最后一行有内容的行号,全空返回 -1
 */
static int fb_detect_last_row(struct fbcon_state *st) {
    if (!st || !st->fb_addr || st->rows <= 0) {
        return -1;
    }

    for (int row = st->rows - 1; row >= 0; row--) {
        int py_start = row * CHAR_HEIGHT;
        int py_end   = py_start + CHAR_HEIGHT;
        if ((uint32_t)py_end > st->fb_height) {
            py_end = (int)st->fb_height;
        }
        for (int y = py_start; y < py_end; y++) {
            uint8_t *line        = st->fb_addr + (uint32_t)y * st->fb_pitch;
            uint32_t check_bytes = st->fb_width * st->bytes_per_pixel;
            for (uint32_t i = 0; i < check_bytes; i++) {
                if (line[i] != 0) {
                    return row;
                }
            }
        }
    }
    return -1;
}

static void fb_scroll_chars(struct fbcon_state *st, int lines) {
    if (!st || !st->fb_addr || lines <= 0) {
        return;
    }
    int scroll_pixels = lines * CHAR_HEIGHT;
    if (scroll_pixels <= 0) {
        return;
    }
    if (scroll_pixels >= (int)st->fb_height) {
        fb_fill_rect(st, 0, 0, (int)st->fb_width, (int)st->fb_height, st->bg_color);
        st->cursor_x = 0;
        st->cursor_y = 0;
        return;
    }

    uint32_t move_lines = st->fb_height - (uint32_t)scroll_pixels;
    memmove(st->fb_addr, st->fb_addr + (uint32_t)scroll_pixels * st->fb_pitch,
            move_lines * st->fb_pitch);
    fb_fill_rect(st, 0, (int)move_lines, (int)st->fb_width, scroll_pixels, st->bg_color);
}

static void fb_draw_glyph_8x16(struct fbcon_state *st, int px, int py, const uint8_t *glyph) {
    if (!st || !glyph) {
        return;
    }
    for (int row = 0; row < CHAR_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < CHAR_WIDTH; col++) {
            uint32_t color = (bits & (1u << (7 - col))) ? st->fg_color : st->bg_color;
            fb_putpixel(st, px + col, py + row, color);
        }
    }
}

static void fb_apply_color(struct fbcon_state *st, uint8_t fg, uint8_t bg) {
    static const uint8_t palette[16][3] = {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
        {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
        {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
        {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
    };

    st->cur_fg = (uint8_t)(fg & 0x0Fu);
    st->cur_bg = (uint8_t)(bg & 0x0Fu);

    st->fg_color =
        fb_make_color(st, palette[st->cur_fg][0], palette[st->cur_fg][1], palette[st->cur_fg][2]);
    st->bg_color =
        fb_make_color(st, palette[st->cur_bg][0], palette[st->cur_bg][1], palette[st->cur_bg][2]);
}

static void fbcon_newline(struct fbcon_state *st) {
    st->cursor_x = 0;
    st->cursor_y++;
    if (st->cursor_y >= st->rows) {
        fb_scroll_chars(st, 1);
        st->cursor_y = st->rows - 1;
    }
}

static void fbcon_putc(struct fbcon_state *st, uint32_t codepoint) {
    if (!st) {
        return;
    }

    if (codepoint == '\n') {
        fbcon_newline(st);
        return;
    }
    if (codepoint == '\r') {
        st->cursor_x = 0;
        return;
    }
    if (codepoint == '\t') {
        int next = (st->cursor_x + 8) & ~7;
        if (next >= st->cols) {
            fbcon_newline(st);
        } else {
            st->cursor_x = next;
        }
        return;
    }
    if (codepoint == '\b') {
        if (st->cursor_x > 0) {
            st->cursor_x--;
            fb_fill_rect(st, st->cursor_x * CHAR_WIDTH, st->cursor_y * CHAR_HEIGHT, CHAR_WIDTH,
                         CHAR_HEIGHT, st->bg_color);
        }
        return;
    }

    if (st->cursor_x >= st->cols) {
        fbcon_newline(st);
    }

    const uint8_t *glyph = font_get_ascii_8x16(codepoint);
    if (!glyph) {
        glyph = font_get_ascii_8x16((uint32_t)'?');
    }
    if (glyph) {
        fb_draw_glyph_8x16(st, st->cursor_x * CHAR_WIDTH, st->cursor_y * CHAR_HEIGHT, glyph);
    }

    st->cursor_x++;
    if (st->cursor_x >= st->cols) {
        fbcon_newline(st);
    }
}

static void fbcon_write_bytes(struct fbcon_state *st, const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint32_t cp = 0;
        size_t   n  = utf8_decode_next(data + i, len - i, &cp);
        if (n == 0) {
            break;
        }
        i += n;
        if (cp > 0x7Fu) {
            cp = (uint32_t)'?';
        }
        fbcon_putc(st, cp);
    }
}

static int console_handler(struct ipc_message *msg) {
    uint32_t opcode = msg->regs.data[0];

    pthread_mutex_lock(&g_fbcon.lock);
    switch (opcode) {
    case UDM_CONSOLE_PUTC: {
        uint32_t v = msg->regs.data[1];
        fbcon_putc(&g_fbcon, (uint32_t)(v & 0xFFu));
        break;
    }
    case UDM_CONSOLE_WRITE: {
        const uint8_t *data = (const uint8_t *)&msg->regs.data[1];
        size_t         len  = (size_t)(msg->regs.data[7] & 0xFFu);
        if (len > UDM_CONSOLE_WRITE_MAX) {
            len = UDM_CONSOLE_WRITE_MAX;
        }
        fbcon_write_bytes(&g_fbcon, data, len);
        break;
    }
    case UDM_CONSOLE_SET_COLOR: {
        uint8_t attr = (uint8_t)(msg->regs.data[1] & 0xFFu);
        fb_apply_color(&g_fbcon, attr & 0x0Fu, (attr >> 4) & 0x0Fu);
        break;
    }
    case UDM_CONSOLE_RESET_COLOR:
        fb_apply_color(&g_fbcon, 7, 0);
        break;
    case UDM_CONSOLE_CLEAR:
        fb_fill_rect(&g_fbcon, 0, 0, (int)g_fbcon.fb_width, (int)g_fbcon.fb_height,
                     g_fbcon.bg_color);
        g_fbcon.cursor_x = 0;
        g_fbcon.cursor_y = 0;
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&g_fbcon.lock);
    return 0;
}

int main(void) {
    env_set_name("fbcond");
    handle_t serial = env_get_handle("serial");

    handle_t fb_handle = env_get_handle("fb_mem");
    if (fb_handle == HANDLE_INVALID) {
        if (serial != HANDLE_INVALID) {
            const char        *msg = "[fbcond] fb_mem missing\n";
            struct ipc_message m   = {0};
            m.regs.data[0]         = UDM_CONSOLE_WRITE;
            m.regs.data[7]         = 23;
            memcpy(&m.regs.data[1], msg, 23);
            sys_ipc_send(serial, &m, 100);
        }
        return 1;
    }

    struct physmem_info pinfo;
    if (sys_physmem_info(fb_handle, &pinfo) < 0 || pinfo.type != 1) {
        return 1;
    }
    if (pinfo.bpp != 24 && pinfo.bpp != 32) {
        return 1;
    }

    uint32_t mapped_size = 0;
    uint8_t *fb_addr     = (uint8_t *)sys_mmap_phys(fb_handle, 0, 0, 0x03, &mapped_size);
    if (!fb_addr || (intptr_t)fb_addr < 0) {
        return 1;
    }

    memset(&g_fbcon, 0, sizeof(g_fbcon));
    g_fbcon.fb_addr         = fb_addr;
    g_fbcon.fb_width        = pinfo.width;
    g_fbcon.fb_height       = pinfo.height;
    g_fbcon.fb_pitch        = pinfo.pitch;
    g_fbcon.fb_bpp          = pinfo.bpp;
    g_fbcon.bytes_per_pixel = (uint8_t)(pinfo.bpp / 8);
    g_fbcon.red_pos         = pinfo.red_pos;
    g_fbcon.red_size        = pinfo.red_size;
    g_fbcon.green_pos       = pinfo.green_pos;
    g_fbcon.green_size      = pinfo.green_size;
    g_fbcon.blue_pos        = pinfo.blue_pos;
    g_fbcon.blue_size       = pinfo.blue_size;
    g_fbcon.cols            = (int)(g_fbcon.fb_width / CHAR_WIDTH);
    g_fbcon.rows            = (int)(g_fbcon.fb_height / CHAR_HEIGHT);
    g_fbcon.cursor_x        = 0;
    g_fbcon.cursor_y        = 0;
    pthread_mutex_init(&g_fbcon.lock, NULL);
    fb_apply_color(&g_fbcon, 7, 0);

    /* 不清屏 - 保留内核启动信息,检测内核光标位置 */
    int last_row = fb_detect_last_row(&g_fbcon);
    if (last_row >= 0) {
        g_fbcon.cursor_y = last_row + 1;
        if (g_fbcon.cursor_y >= g_fbcon.rows) {
            g_fbcon.cursor_y = g_fbcon.rows - 1;
        }
    }

    handle_t fbcon_ep = env_require("fbcon_ep");
    if (fbcon_ep == HANDLE_INVALID) {
        return 1;
    }

    struct udm_server srv = {
        .endpoint = fbcon_ep,
        .handler  = console_handler,
        .name     = "fbcond",
    };

    udm_server_init(&srv);
    svc_notify_ready("fbcond");

    if (serial != HANDLE_INVALID) {
        const char        *msg = "[fbcond] ready\n";
        struct ipc_message m   = {0};
        m.regs.data[0]         = UDM_CONSOLE_WRITE;
        m.regs.data[7]         = 14;
        memcpy(&m.regs.data[1], msg, 14);
        sys_ipc_send(serial, &m, 100);
    }

    udm_server_run(&srv);
    return 0;
}
