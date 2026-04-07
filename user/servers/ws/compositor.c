/**
 * @file compositor.c
 * @brief 合成器实现
 */

#include "compositor.h"
#include "cursor.h"
#include "window.h"
#include "wsd.h"

#include <font/font.h>
#include <string.h>

/* 从 ARGB32 颜色分量提取 */
#define ARGB_R(c) (((c) >> 16) & 0xFF)
#define ARGB_G(c) (((c) >> 8) & 0xFF)
#define ARGB_B(c) ((c) & 0xFF)

void compositor_init(struct ws_server *srv) {
    srv->fb_bpp_bytes = srv->fb_info.bpp / 8;

    /* 检测是否可以跳过颜色转换 (ARGB32 == fb 原生格式) */
    srv->fb_is_argb32 = (srv->fb_info.bpp == 32 &&
                         srv->fb_info.red_pos == 16 && srv->fb_info.red_size == 8 &&
                         srv->fb_info.green_pos == 8 && srv->fb_info.green_size == 8 &&
                         srv->fb_info.blue_pos == 0 && srv->fb_info.blue_size == 8);
}

/**
 * 像素地址 - 合成时写后台缓冲区
 */
static inline uint8_t *pixel_addr(struct ws_server *srv, int x, int y) {
    return srv->backbuf + (uint32_t)y * srv->fb_info.pitch +
           (uint32_t)x * srv->fb_bpp_bytes;
}

/**
 * 前台 framebuffer 像素地址 - 仅用于光标 save/restore
 */
static inline uint8_t *fb_pixel_addr(struct ws_server *srv, int x, int y) {
    return srv->fb_addr + (uint32_t)y * srv->fb_info.pitch +
           (uint32_t)x * srv->fb_bpp_bytes;
}

uint32_t fb_make_color(struct ws_server *srv, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0;
    color |= ((uint32_t)r >> (8 - srv->fb_info.red_size)) << srv->fb_info.red_pos;
    color |= ((uint32_t)g >> (8 - srv->fb_info.green_size))
             << srv->fb_info.green_pos;
    color |= ((uint32_t)b >> (8 - srv->fb_info.blue_size))
             << srv->fb_info.blue_pos;
    return color;
}

void fb_put_pixel(struct ws_server *srv, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= srv->fb_info.width ||
        (uint32_t)y >= srv->fb_info.height)
        return;

    if (srv->fb_bpp_bytes == 4) {
        *(uint32_t *)pixel_addr(srv, x, y) = color;
    } else if (srv->fb_bpp_bytes == 3) {
        uint8_t *p = pixel_addr(srv, x, y);
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
    }
}

uint32_t fb_get_pixel(struct ws_server *srv, int x, int y) {
    if (x < 0 || y < 0 || (uint32_t)x >= srv->fb_info.width ||
        (uint32_t)y >= srv->fb_info.height)
        return 0;

    if (srv->fb_bpp_bytes == 4) {
        return *(uint32_t *)pixel_addr(srv, x, y);
    } else if (srv->fb_bpp_bytes == 3) {
        uint8_t *p = pixel_addr(srv, x, y);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    return 0;
}

void fb_put_pixel_front(struct ws_server *srv, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= srv->fb_info.width ||
        (uint32_t)y >= srv->fb_info.height)
        return;
    if (srv->fb_bpp_bytes == 4) {
        *(uint32_t *)fb_pixel_addr(srv, x, y) = color;
    } else if (srv->fb_bpp_bytes == 3) {
        uint8_t *p = fb_pixel_addr(srv, x, y);
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
    }
}

uint32_t fb_get_pixel_front(struct ws_server *srv, int x, int y) {
    if (x < 0 || y < 0 || (uint32_t)x >= srv->fb_info.width ||
        (uint32_t)y >= srv->fb_info.height)
        return 0;
    if (srv->fb_bpp_bytes == 4) {
        return *(uint32_t *)fb_pixel_addr(srv, x, y);
    } else if (srv->fb_bpp_bytes == 3) {
        uint8_t *p = fb_pixel_addr(srv, x, y);
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    return 0;
}

void fb_fill_rect(struct ws_server *srv, int x, int y, int w, int h,
                  uint32_t color) {
    /* 裁剪 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)srv->fb_info.width)
        w = (int)srv->fb_info.width - x;
    if (y + h > (int)srv->fb_info.height)
        h = (int)srv->fb_info.height - y;
    if (w <= 0 || h <= 0)
        return;

    if (srv->fb_bpp_bytes == 4) {
        uint32_t *first_line = (uint32_t *)pixel_addr(srv, x, y);
        for (int i = 0; i < w; i++)
            first_line[i] = color;

        size_t row_bytes = (size_t)w * 4;
        for (int row = y + 1; row < y + h; row++) {
            memcpy(pixel_addr(srv, x, row), first_line, row_bytes);
        }
    } else if (srv->fb_bpp_bytes == 3) {
        uint8_t b0 = color & 0xFF;
        uint8_t b1 = (color >> 8) & 0xFF;
        uint8_t b2 = (color >> 16) & 0xFF;
        for (int row = y; row < y + h; row++) {
            uint8_t *line = pixel_addr(srv, x, row);
            for (int i = 0; i < w; i++) {
                line[i * 3]     = b0;
                line[i * 3 + 1] = b1;
                line[i * 3 + 2] = b2;
            }
        }
    }
}

/**
 * 将 ARGB32 像素转换为 fb 原生颜色
 */
static inline uint32_t argb_to_fb(struct ws_server *srv, uint32_t argb) {
    return fb_make_color(srv, ARGB_R(argb), ARGB_G(argb), ARGB_B(argb));
}

/**
 * 绘制 8x16 字符到 framebuffer
 */
static void fb_draw_char(struct ws_server *srv, int px, int py,
                         char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font_get_ascii_8x16((uint32_t)(uint8_t)c);
    if (!glyph)
        return;

    for (int row = 0; row < (int)FONT_ASCII_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < (int)FONT_ASCII_WIDTH; col++) {
            uint32_t color = (bits & (1u << (7 - col))) ? fg : bg;
            fb_put_pixel(srv, px + col, py + row, color);
        }
    }
}

/**
 * 绘制文字到 framebuffer
 */
static void fb_draw_text(struct ws_server *srv, int px, int py,
                         const char *text, uint32_t fg, uint32_t bg) {
    for (int i = 0; text[i]; i++) {
        fb_draw_char(srv, px + i * (int)FONT_ASCII_WIDTH, py, text[i], fg, bg);
    }
}

/**
 * 绘制矩形边框
 */
static void fb_draw_rect_outline(struct ws_server *srv, int x, int y,
                                 int w, int h, uint32_t color) {
    fb_fill_rect(srv, x, y, w, 1, color);                /* 上 */
    fb_fill_rect(srv, x, y + h - 1, w, 1, color);        /* 下 */
    fb_fill_rect(srv, x, y, 1, h, color);                /* 左 */
    fb_fill_rect(srv, x + w - 1, y, 1, h, color);        /* 右 */
}

/**
 * 绘制 X 关闭符号
 */
static void fb_draw_x(struct ws_server *srv, int x, int y,
                       int size, uint32_t color) {
    int margin = 3;
    for (int i = margin; i < size - margin; i++) {
        fb_put_pixel(srv, x + i, y + i, color);
        fb_put_pixel(srv, x + i, y + size - 1 - i, color);
        /* 加粗 */
        fb_put_pixel(srv, x + i + 1, y + i, color);
        fb_put_pixel(srv, x + i + 1, y + size - 1 - i, color);
    }
}

/**
 * blit 客户端 ARGB32 SHM 到后台缓冲区
 */
static int rect_intersects(int x1, int y1, int w1, int h1,
                           int x2, int y2, int w2, int h2) {
    return !(x1 + w1 <= x2 || x2 + w2 <= x1 || y1 + h1 <= y2 || y2 + h2 <= y1);
}

static void blit_window_client_region(struct ws_server *srv, struct ws_window *win,
                                      int clip_x1, int clip_y1, int clip_x2, int clip_y2) {
    int dx = window_client_x(win);
    int dy = window_client_y(win);
    int w = win->client_w;
    int h = win->client_h;
    const uint32_t *src = win->shm_addr;

    if (!src)
        return;

    for (int row = 0; row < h; row++) {
        int sy = dy + row;
        if (sy < 0 || (uint32_t)sy >= srv->fb_info.height)
            continue;

        if (sy < clip_y1 || sy >= clip_y2)
            continue;

        /* 计算本行有效像素范围 */
        int x_start = dx < 0 ? -dx : 0;
        int x_end = w;
        if (dx + x_end > (int)srv->fb_info.width)
            x_end = (int)srv->fb_info.width - dx;
        if (dx + x_start < clip_x1)
            x_start = clip_x1 - dx;
        if (dx + x_end > clip_x2)
            x_end = clip_x2 - dx;
        if (x_start >= x_end)
            continue;

        int visible_w = x_end - x_start;

        if (srv->fb_is_argb32) {
            /* 快速路径: 直接 memcpy 整行 */
            memcpy(pixel_addr(srv, dx + x_start, sy),
                   &src[row * w + x_start],
                   (size_t)visible_w * 4);
        } else {
            /* 慢速路径: 逐像素转换 */
            for (int col = x_start; col < x_end; col++) {
                uint32_t argb = src[row * w + col];
                fb_put_pixel(srv, dx + col, sy, argb_to_fb(srv, argb));
            }
        }
    }
}

/**
 * 绘制单个窗口 (装饰 + 客户区)
 */
static void blit_window_region(struct ws_server *srv, struct ws_window *win,
                               int clip_x1, int clip_y1, int clip_x2, int clip_y2) {
    int32_t ow = window_outer_w(win);
    int32_t oh = window_outer_h(win);

    if (!rect_intersects(win->x, win->y, ow, oh,
                         clip_x1, clip_y1, clip_x2 - clip_x1, clip_y2 - clip_y1)) {
        return;
    }

    if (!(win->flags & WS_FLAG_NO_DECOR)) {
        /* 边框 */
        uint32_t border_color = argb_to_fb(srv, WS_COLOR_BORDER);
        fb_draw_rect_outline(srv, win->x, win->y, ow, oh, border_color);

        /* 标题栏 */
        int32_t tb_x = win->x + WS_BORDER_WIDTH;
        int32_t tb_y = win->y + WS_BORDER_WIDTH;
        int32_t tb_w = win->client_w;

        uint32_t tb_argb = win->focused
            ? WS_COLOR_TITLEBAR_ACTIVE : WS_COLOR_TITLEBAR_INACTIVE;
        uint32_t tb_color = argb_to_fb(srv, tb_argb);
        fb_fill_rect(srv, tb_x, tb_y, tb_w, WS_TITLEBAR_HEIGHT, tb_color);

        /* 标题文字 */
        uint32_t text_color = argb_to_fb(srv, WS_COLOR_TITLE_TEXT);
        fb_draw_text(srv, tb_x + 4,
                     tb_y + (WS_TITLEBAR_HEIGHT - (int)FONT_ASCII_HEIGHT) / 2,
                     win->title, text_color, tb_color);

        /* 关闭按钮 */
        int32_t close_x = tb_x + tb_w - WS_CLOSE_BTN_SIZE - 4;
        int32_t close_y = tb_y + (WS_TITLEBAR_HEIGHT - WS_CLOSE_BTN_SIZE) / 2;
        uint32_t close_bg = argb_to_fb(srv, WS_COLOR_CLOSE_BG);
        uint32_t close_fg = argb_to_fb(srv, WS_COLOR_CLOSE_FG);
        fb_fill_rect(srv, close_x, close_y,
                     WS_CLOSE_BTN_SIZE, WS_CLOSE_BTN_SIZE, close_bg);
        fb_draw_x(srv, close_x, close_y, WS_CLOSE_BTN_SIZE, close_fg);
    }

    /* 客户区 */
    blit_window_client_region(srv, win, clip_x1, clip_y1, clip_x2, clip_y2);
}

/**
 * 只拷贝 backbuf 中指定矩形区域到前台 framebuffer
 */
static void flip_region(struct ws_server *srv, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)srv->fb_info.width)
        w = (int)srv->fb_info.width - x;
    if (y + h > (int)srv->fb_info.height)
        h = (int)srv->fb_info.height - y;
    if (w <= 0 || h <= 0)
        return;

    size_t row_bytes = (size_t)w * srv->fb_bpp_bytes;
    for (int row = y; row < y + h; row++) {
        memcpy(fb_pixel_addr(srv, x, row),
               pixel_addr(srv, x, row),
               row_bytes);
    }
}

void compositor_composite(struct ws_server *srv) {
    /* 首次合成: 全屏清除 */
    if (srv->first_composite) {
        uint32_t bg = argb_to_fb(srv, WS_COLOR_DESKTOP);
        fb_fill_rect(srv, 0, 0, (int)srv->fb_info.width,
                     (int)srv->fb_info.height, bg);

        for (uint32_t i = 0; i < srv->z_count; i++) {
            struct ws_window *win = &srv->windows[srv->z_order[i]];
            if (win->id != 0) {
                blit_window_region(srv, win, 0, 0, (int)srv->fb_info.width,
                                   (int)srv->fb_info.height);
            }
        }
        cursor_draw_backbuf(srv);
        memcpy(srv->fb_addr, srv->backbuf, srv->fb_size);
        cursor_save(srv);
        srv->first_composite = 0;
        srv->needs_composite = 0;
        return;
    }

    /* 计算所有窗口 + 光标的脏区域边界框 */
    int dirty_x1 = (int)srv->fb_info.width;
    int dirty_y1 = (int)srv->fb_info.height;
    int dirty_x2 = 0;
    int dirty_y2 = 0;

    for (uint32_t i = 0; i < srv->z_count; i++) {
        struct ws_window *win = &srv->windows[srv->z_order[i]];
        if (win->id == 0) continue;
        int wx2 = win->x + window_outer_w(win);
        int wy2 = win->y + window_outer_h(win);
        if (win->x < dirty_x1) dirty_x1 = win->x;
        if (win->y < dirty_y1) dirty_y1 = win->y;
        if (wx2 > dirty_x2) dirty_x2 = wx2;
        if (wy2 > dirty_y2) dirty_y2 = wy2;
    }

    /* 光标区域 */
    if (srv->cursor_x < dirty_x1) dirty_x1 = srv->cursor_x;
    if (srv->cursor_y < dirty_y1) dirty_y1 = srv->cursor_y;
    if (srv->cursor_x + WS_CURSOR_W > dirty_x2)
        dirty_x2 = srv->cursor_x + WS_CURSOR_W;
    if (srv->cursor_y + WS_CURSOR_H > dirty_y2)
        dirty_y2 = srv->cursor_y + WS_CURSOR_H;

    /* 上一次光标位置也要包含 */
    if (srv->cursor_visible) {
        if (srv->cursor_saved_x < dirty_x1)
            dirty_x1 = srv->cursor_saved_x;
        if (srv->cursor_saved_y < dirty_y1)
            dirty_y1 = srv->cursor_saved_y;
        if (srv->cursor_saved_x + WS_CURSOR_W > dirty_x2)
            dirty_x2 = srv->cursor_saved_x + WS_CURSOR_W;
        if (srv->cursor_saved_y + WS_CURSOR_H > dirty_y2)
            dirty_y2 = srv->cursor_saved_y + WS_CURSOR_H;
    }

    /* 合并上一帧脏区域 (清除残影) */
    if (srv->prev_dirty_w > 0 && srv->prev_dirty_h > 0) {
        int px2 = srv->prev_dirty_x + srv->prev_dirty_w;
        int py2 = srv->prev_dirty_y + srv->prev_dirty_h;
        if (srv->prev_dirty_x < dirty_x1) dirty_x1 = srv->prev_dirty_x;
        if (srv->prev_dirty_y < dirty_y1) dirty_y1 = srv->prev_dirty_y;
        if (px2 > dirty_x2) dirty_x2 = px2;
        if (py2 > dirty_y2) dirty_y2 = py2;
    }

    /* clamp */
    if (dirty_x1 < 0) dirty_x1 = 0;
    if (dirty_y1 < 0) dirty_y1 = 0;
    if (dirty_x2 > (int)srv->fb_info.width) dirty_x2 = (int)srv->fb_info.width;
    if (dirty_y2 > (int)srv->fb_info.height) dirty_y2 = (int)srv->fb_info.height;

    /* 先在后台缓冲区绘制完整帧 */
    uint32_t bg = argb_to_fb(srv, WS_COLOR_DESKTOP);
    fb_fill_rect(srv, dirty_x1, dirty_y1,
                 dirty_x2 - dirty_x1, dirty_y2 - dirty_y1, bg);

    for (uint32_t i = 0; i < srv->z_count; i++) {
        uint32_t idx = srv->z_order[i];
        struct ws_window *win = &srv->windows[idx];
        if (win->id == 0)
            continue;
        blit_window_region(srv, win, dirty_x1, dirty_y1, dirty_x2, dirty_y2);
    }

    cursor_draw_backbuf(srv);

    /* 只拷贝脏区域到前台 framebuffer */
    flip_region(srv, dirty_x1, dirty_y1,
                dirty_x2 - dirty_x1, dirty_y2 - dirty_y1);

    cursor_save(srv);

    /* 记录本帧脏区域, 下帧用于清除残影 */
    srv->prev_dirty_x = dirty_x1;
    srv->prev_dirty_y = dirty_y1;
    srv->prev_dirty_w = dirty_x2 - dirty_x1;
    srv->prev_dirty_h = dirty_y2 - dirty_y1;

    srv->needs_composite = 0;
}
