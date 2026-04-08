/**
 * @file compositor.c
 * @brief 合成器实现 — 并行条带渲染
 *
 * 事件/IPC 线程标脏后唤醒 render 线程.
 * Render 线程将脏区域按水平条带切分, 多个 worker 并行 blit + flip.
 */

#include "compositor.h"
#include "cursor.h"
#include "window.h"
#include "wsd.h"

#include <font/font.h>
#include <pthread.h>
#include <string.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* ── 配置 ─────────────────────────────────────────────── */

#define RENDER_WORKERS 3

/* ── 快照结构 ──────────────────────────────────────────── */

struct ws_snap_window {
    int32_t   x, y;
    int32_t   client_w, client_h;
    uint32_t  flags;
    uint8_t   focused;
    uint32_t *shm_addr;
    char      title[WS_TITLE_MAX];
};

struct compositor_snapshot {
    uint32_t z_order[WS_MAX_WINDOWS];
    uint32_t z_count;

    struct ws_snap_window windows[WS_MAX_WINDOWS];
    uint8_t               window_active[WS_MAX_WINDOWS];

    int32_t cursor_x, cursor_y;

    uint8_t first_composite;
    uint8_t active;
};

/* ── Worker 结构 ──────────────────────────────────────── */

struct render_band {
    struct ws_server                *srv;
    const struct compositor_snapshot *snap;
    int      clip_x1, clip_y1, clip_x2, clip_y2;
    uint32_t bg_color;

    handle_t start_event;   /* render 线程 signal → worker wait */
    handle_t done_event;    /* worker signal → render 线程 wait */
};

static struct render_band g_bands[RENDER_WORKERS];
static int g_num_workers = 0;

/* ── 颜色宏 ───────────────────────────────────────────── */

#define ARGB_R(c) (((c) >> 16) & 0xFF)
#define ARGB_G(c) (((c) >> 8) & 0xFF)
#define ARGB_B(c) ((c) & 0xFF)

/* ── 低级 framebuffer 操作 ────────────────────────────── */

void compositor_init(struct ws_server *srv) {
    srv->fb_bpp_bytes = srv->fb_info.bpp / 8;
    srv->fb_is_argb32 = (srv->fb_info.bpp == 32 &&
                         srv->fb_info.red_pos == 16 && srv->fb_info.red_size == 8 &&
                         srv->fb_info.green_pos == 8 && srv->fb_info.green_size == 8 &&
                         srv->fb_info.blue_pos == 0 && srv->fb_info.blue_size == 8);
}

static inline uint8_t *pixel_addr(struct ws_server *srv, int x, int y) {
    return srv->backbuf + (uint32_t)y * srv->fb_info.pitch +
           (uint32_t)x * srv->fb_bpp_bytes;
}

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

void fb_fill_rect(struct ws_server *srv, int x, int y, int w, int h,
                  uint32_t color) {
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

/* ── clip 版绘制 (条带安全) ───────────────────────────── */

static void fb_fill_rect_clipped(struct ws_server *srv,
                                 int x, int y, int w, int h,
                                 uint32_t color, int cy1, int cy2) {
    if (y < cy1) { h -= (cy1 - y); y = cy1; }
    if (y + h > cy2) { h = cy2 - y; }
    if (h <= 0) return;
    fb_fill_rect(srv, x, y, w, h, color);
}

static inline void fb_put_pixel_clipped(struct ws_server *srv,
                                        int x, int y, uint32_t color,
                                        int cy1, int cy2) {
    if (y < cy1 || y >= cy2) return;
    fb_put_pixel(srv, x, y, color);
}

/* ── 内部绘制辅助 ────────────────────────────────────── */

static inline uint32_t argb_to_fb(struct ws_server *srv, uint32_t argb) {
    return fb_make_color(srv, ARGB_R(argb), ARGB_G(argb), ARGB_B(argb));
}

static void fb_draw_char_clipped(struct ws_server *srv, int px, int py,
                                 char c, uint32_t fg, uint32_t bg,
                                 int cy1, int cy2) {
    const uint8_t *glyph = font_get_ascii_8x16((uint32_t)(uint8_t)c);
    if (!glyph) return;
    for (int row = 0; row < (int)FONT_ASCII_HEIGHT; row++) {
        int sy = py + row;
        if (sy < cy1 || sy >= cy2) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < (int)FONT_ASCII_WIDTH; col++) {
            uint32_t color = (bits & (1u << (7 - col))) ? fg : bg;
            fb_put_pixel(srv, px + col, sy, color);
        }
    }
}

static void fb_draw_text_clipped(struct ws_server *srv, int px, int py,
                                 const char *text, uint32_t fg, uint32_t bg,
                                 int cy1, int cy2) {
    for (int i = 0; text[i]; i++) {
        fb_draw_char_clipped(srv, px + i * (int)FONT_ASCII_WIDTH, py,
                             text[i], fg, bg, cy1, cy2);
    }
}

static void fb_draw_rect_outline_clipped(struct ws_server *srv,
                                         int x, int y, int w, int h,
                                         uint32_t color, int cy1, int cy2) {
    fb_fill_rect_clipped(srv, x, y, w, 1, color, cy1, cy2);
    fb_fill_rect_clipped(srv, x, y + h - 1, w, 1, color, cy1, cy2);
    fb_fill_rect_clipped(srv, x, y, 1, h, color, cy1, cy2);
    fb_fill_rect_clipped(srv, x + w - 1, y, 1, h, color, cy1, cy2);
}

static void fb_draw_x_clipped(struct ws_server *srv, int x, int y,
                               int size, uint32_t color, int cy1, int cy2) {
    int margin = 3;
    for (int i = margin; i < size - margin; i++) {
        fb_put_pixel_clipped(srv, x + i, y + i, color, cy1, cy2);
        fb_put_pixel_clipped(srv, x + i, y + size - 1 - i, color, cy1, cy2);
        fb_put_pixel_clipped(srv, x + i + 1, y + i, color, cy1, cy2);
        fb_put_pixel_clipped(srv, x + i + 1, y + size - 1 - i, color, cy1, cy2);
    }
}

/* ── snap_window 几何辅助 ─────────────────────────────── */

static inline int32_t snap_outer_w(const struct ws_snap_window *sw) {
    if (sw->flags & WS_FLAG_NO_DECOR) return sw->client_w;
    return sw->client_w + 2 * WS_BORDER_WIDTH;
}

static inline int32_t snap_outer_h(const struct ws_snap_window *sw) {
    if (sw->flags & WS_FLAG_NO_DECOR) return sw->client_h;
    return sw->client_h + WS_TITLEBAR_HEIGHT + 2 * WS_BORDER_WIDTH;
}

static inline int32_t snap_client_x(const struct ws_snap_window *sw) {
    if (sw->flags & WS_FLAG_NO_DECOR) return sw->x;
    return sw->x + WS_BORDER_WIDTH;
}

static inline int32_t snap_client_y(const struct ws_snap_window *sw) {
    if (sw->flags & WS_FLAG_NO_DECOR) return sw->y;
    return sw->y + WS_BORDER_WIDTH + WS_TITLEBAR_HEIGHT;
}

/* ── blit (严格 clip 到条带) ──────────────────────────── */

static int rect_intersects(int x1, int y1, int w1, int h1,
                           int x2, int y2, int w2, int h2) {
    return !(x1 + w1 <= x2 || x2 + w2 <= x1 || y1 + h1 <= y2 || y2 + h2 <= y1);
}

static void blit_snap_client_region(struct ws_server *srv,
                                    const struct ws_snap_window *sw,
                                    int clip_x1, int clip_y1,
                                    int clip_x2, int clip_y2) {
    int dx = snap_client_x(sw);
    int dy = snap_client_y(sw);
    int w = sw->client_w;
    int h = sw->client_h;
    const uint32_t *src = sw->shm_addr;
    if (!src) return;

    for (int row = 0; row < h; row++) {
        int sy = dy + row;
        if (sy < 0 || (uint32_t)sy >= srv->fb_info.height) continue;
        if (sy < clip_y1 || sy >= clip_y2) continue;

        int x_start = dx < 0 ? -dx : 0;
        int x_end = w;
        if (dx + x_end > (int)srv->fb_info.width) x_end = (int)srv->fb_info.width - dx;
        if (dx + x_start < clip_x1) x_start = clip_x1 - dx;
        if (dx + x_end > clip_x2)   x_end = clip_x2 - dx;
        if (x_start >= x_end) continue;

        int visible_w = x_end - x_start;
        if (srv->fb_is_argb32) {
            memcpy(pixel_addr(srv, dx + x_start, sy),
                   &src[row * w + x_start],
                   (size_t)visible_w * 4);
        } else {
            for (int col = x_start; col < x_end; col++) {
                uint32_t argb = src[row * w + col];
                fb_put_pixel(srv, dx + col, sy, argb_to_fb(srv, argb));
            }
        }
    }
}

static void blit_snap_window_region(struct ws_server *srv,
                                    const struct ws_snap_window *sw,
                                    int clip_x1, int clip_y1,
                                    int clip_x2, int clip_y2) {
    int32_t ow = snap_outer_w(sw);
    int32_t oh = snap_outer_h(sw);

    if (!rect_intersects(sw->x, sw->y, ow, oh,
                         clip_x1, clip_y1, clip_x2 - clip_x1, clip_y2 - clip_y1))
        return;

    if (!(sw->flags & WS_FLAG_NO_DECOR)) {
        uint32_t border_color = argb_to_fb(srv, WS_COLOR_BORDER);
        fb_draw_rect_outline_clipped(srv, sw->x, sw->y, ow, oh,
                                     border_color, clip_y1, clip_y2);

        int32_t tb_x = sw->x + WS_BORDER_WIDTH;
        int32_t tb_y = sw->y + WS_BORDER_WIDTH;
        int32_t tb_w = sw->client_w;

        uint32_t tb_argb = sw->focused
            ? WS_COLOR_TITLEBAR_ACTIVE : WS_COLOR_TITLEBAR_INACTIVE;
        uint32_t tb_color = argb_to_fb(srv, tb_argb);
        fb_fill_rect_clipped(srv, tb_x, tb_y, tb_w, WS_TITLEBAR_HEIGHT,
                             tb_color, clip_y1, clip_y2);

        uint32_t text_color = argb_to_fb(srv, WS_COLOR_TITLE_TEXT);
        fb_draw_text_clipped(srv, tb_x + 4,
                             tb_y + (WS_TITLEBAR_HEIGHT - (int)FONT_ASCII_HEIGHT) / 2,
                             sw->title, text_color, tb_color, clip_y1, clip_y2);

        int32_t close_x = tb_x + tb_w - WS_CLOSE_BTN_SIZE - 4;
        int32_t close_y = tb_y + (WS_TITLEBAR_HEIGHT - WS_CLOSE_BTN_SIZE) / 2;
        uint32_t close_bg = argb_to_fb(srv, WS_COLOR_CLOSE_BG);
        uint32_t close_fg = argb_to_fb(srv, WS_COLOR_CLOSE_FG);
        fb_fill_rect_clipped(srv, close_x, close_y,
                             WS_CLOSE_BTN_SIZE, WS_CLOSE_BTN_SIZE,
                             close_bg, clip_y1, clip_y2);
        fb_draw_x_clipped(srv, close_x, close_y,
                          WS_CLOSE_BTN_SIZE, close_fg, clip_y1, clip_y2);
    }

    blit_snap_client_region(srv, sw, clip_x1, clip_y1, clip_x2, clip_y2);
}

/* ── flip ─────────────────────────────────────────────── */

static void flip_region(struct ws_server *srv, int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)srv->fb_info.width)  w = (int)srv->fb_info.width - x;
    if (y + h > (int)srv->fb_info.height) h = (int)srv->fb_info.height - y;
    if (w <= 0 || h <= 0) return;
    size_t row_bytes = (size_t)w * srv->fb_bpp_bytes;
    for (int row = y; row < y + h; row++) {
        memcpy(fb_pixel_addr(srv, x, row), pixel_addr(srv, x, row), row_bytes);
    }
}

/* ── 条带合成: bg + blit + cursor + flip ──────────────── */

static void composite_band(struct ws_server *srv,
                           const struct compositor_snapshot *snap,
                           uint32_t bg_color,
                           int cx1, int cy1, int cx2, int cy2) {
    if (cx1 >= cx2 || cy1 >= cy2)
        return;

    fb_fill_rect(srv, cx1, cy1, cx2 - cx1, cy2 - cy1, bg_color);

    for (uint32_t i = 0; i < snap->z_count; i++) {
        uint32_t idx = snap->z_order[i];
        if (!snap->window_active[idx])
            continue;
        blit_snap_window_region(srv, &snap->windows[idx],
                                cx1, cy1, cx2, cy2);
    }

    if (rect_intersects(snap->cursor_x, snap->cursor_y,
                        WS_CURSOR_W, WS_CURSOR_H,
                        cx1, cy1, cx2 - cx1, cy2 - cy1)) {
        cursor_draw_backbuf(srv, snap->cursor_x, snap->cursor_y);
    }

    flip_region(srv, cx1, cy1, cx2 - cx1, cy2 - cy1);
}

/* ── Worker 线程 ──────────────────────────────────────── */

static void *render_worker_thread(void *arg) {
    struct render_band *band = (struct render_band *)arg;

    while (1) {
        sys_event_wait(band->start_event);

        composite_band(band->srv, band->snap, band->bg_color,
                       band->clip_x1, band->clip_y1,
                       band->clip_x2, band->clip_y2);

        sys_event_signal(band->done_event, 1);
    }
    return NULL;
}

/* ── 快照采集 (在 lock 内调用) ────────────────────────── */

static void compositor_take_snapshot(struct ws_server *srv,
                                     struct compositor_snapshot *snap) {
    snap->z_count = srv->z_count;
    memcpy(snap->z_order, srv->z_order, sizeof(uint32_t) * srv->z_count);

    for (uint32_t i = 0; i < WS_MAX_WINDOWS; i++) {
        struct ws_window *w = &srv->windows[i];
        snap->window_active[i] = (w->id != 0) ? 1 : 0;
        if (w->id != 0) {
            struct ws_snap_window *sw = &snap->windows[i];
            sw->x        = w->x;
            sw->y        = w->y;
            sw->client_w = w->client_w;
            sw->client_h = w->client_h;
            sw->flags    = w->flags;
            sw->focused  = w->focused;
            sw->shm_addr = w->shm_addr;
            memcpy(sw->title, w->title, WS_TITLE_MAX);
        }
    }

    snap->cursor_x = srv->cursor_x;
    snap->cursor_y = srv->cursor_y;
    snap->first_composite = srv->first_composite;
    snap->active          = srv->active;

    srv->first_composite = 0;
    srv->needs_composite = 0;
}

/* ── 初始化 workers ──────────────────────────────────── */

static void init_workers(struct ws_server *srv) {
    for (int i = 0; i < RENDER_WORKERS; i++) {
        g_bands[i].srv = srv;

        int se = sys_event_create();
        int de = sys_event_create();
        if (se < 0 || de < 0) {
            ulog_errf("[ws] Failed to create worker events (worker %d)\n", i);
            if (se >= 0) sys_handle_close((handle_t)se);
            if (de >= 0) sys_handle_close((handle_t)de);
            break;
        }
        g_bands[i].start_event = (handle_t)se;
        g_bands[i].done_event  = (handle_t)de;

        pthread_t tid;
        if (pthread_create(&tid, NULL, render_worker_thread, &g_bands[i]) != 0) {
            ulog_errf("[ws] Failed to create render worker %d\n", i);
            sys_handle_close(g_bands[i].start_event);
            sys_handle_close(g_bands[i].done_event);
            break;
        }
        g_num_workers++;
    }
    ulog_tagf(stdout, TERM_COLOR_WHITE, "[ws]",
              " %d render workers ready\n", g_num_workers);
}

/* ── 并行合成 ─────────────────────────────────────────── */

static void composite_parallel(struct ws_server *srv,
                               const struct compositor_snapshot *snap,
                               int dx1, int dy1, int dx2, int dy2) {
    uint32_t bg = argb_to_fb(srv, WS_COLOR_DESKTOP);
    int total_h = dy2 - dy1;
    int n_bands = g_num_workers + 1;

    if (total_h < n_bands * 4 || g_num_workers == 0) {
        composite_band(srv, snap, bg, dx1, dy1, dx2, dy2);
        return;
    }

    int band_h = total_h / n_bands;

    /* 分发给 workers */
    for (int i = 0; i < g_num_workers; i++) {
        int by1 = dy1 + (i + 1) * band_h;
        int by2 = (i == g_num_workers - 1) ? dy2 : dy1 + (i + 2) * band_h;
        g_bands[i].snap     = snap;
        g_bands[i].bg_color = bg;
        g_bands[i].clip_x1  = dx1;
        g_bands[i].clip_y1  = by1;
        g_bands[i].clip_x2  = dx2;
        g_bands[i].clip_y2  = by2;
        sys_event_signal(g_bands[i].start_event, 1);
    }

    /* 主线程处理 band 0 */
    composite_band(srv, snap, bg, dx1, dy1, dx2, dy1 + band_h);

    /* 等 workers 完成 */
    for (int i = 0; i < g_num_workers; i++) {
        sys_event_wait(g_bands[i].done_event);
    }
}

/* ── 渲染线程主循环 ──────────────────────────────────── */

void *compositor_render_thread(void *arg) {
    struct ws_server *srv = (struct ws_server *)arg;
    struct compositor_snapshot snap;
    int32_t prev_dirty_x = 0, prev_dirty_y = 0;
    int32_t prev_dirty_w = 0, prev_dirty_h = 0;

    memset(&snap, 0, sizeof(snap));
    init_workers(srv);

    while (1) {
        sys_event_wait(srv->render_event);

        pthread_mutex_lock(&srv->lock);
        if (!srv->needs_composite) {
            pthread_mutex_unlock(&srv->lock);
            continue;
        }
        compositor_take_snapshot(srv, &snap);
        pthread_mutex_unlock(&srv->lock);

        if (!snap.active)
            continue;

        if (snap.first_composite) {
            composite_parallel(srv, &snap, 0, 0,
                               (int)srv->fb_info.width,
                               (int)srv->fb_info.height);
            prev_dirty_x = prev_dirty_y = 0;
            prev_dirty_w = prev_dirty_h = 0;
            continue;
        }

        /* 计算脏区域 */
        int dirty_x1 = (int)srv->fb_info.width;
        int dirty_y1 = (int)srv->fb_info.height;
        int dirty_x2 = 0;
        int dirty_y2 = 0;

        for (uint32_t i = 0; i < snap.z_count; i++) {
            uint32_t idx = snap.z_order[i];
            if (!snap.window_active[idx]) continue;
            const struct ws_snap_window *sw = &snap.windows[idx];
            int wx2 = sw->x + snap_outer_w(sw);
            int wy2 = sw->y + snap_outer_h(sw);
            if (sw->x < dirty_x1) dirty_x1 = sw->x;
            if (sw->y < dirty_y1) dirty_y1 = sw->y;
            if (wx2 > dirty_x2)   dirty_x2 = wx2;
            if (wy2 > dirty_y2)   dirty_y2 = wy2;
        }

        if (snap.cursor_x < dirty_x1)               dirty_x1 = snap.cursor_x;
        if (snap.cursor_y < dirty_y1)               dirty_y1 = snap.cursor_y;
        if (snap.cursor_x + WS_CURSOR_W > dirty_x2) dirty_x2 = snap.cursor_x + WS_CURSOR_W;
        if (snap.cursor_y + WS_CURSOR_H > dirty_y2) dirty_y2 = snap.cursor_y + WS_CURSOR_H;

        if (prev_dirty_w > 0 && prev_dirty_h > 0) {
            int px2 = prev_dirty_x + prev_dirty_w;
            int py2 = prev_dirty_y + prev_dirty_h;
            if (prev_dirty_x < dirty_x1) dirty_x1 = prev_dirty_x;
            if (prev_dirty_y < dirty_y1) dirty_y1 = prev_dirty_y;
            if (px2 > dirty_x2)          dirty_x2 = px2;
            if (py2 > dirty_y2)          dirty_y2 = py2;
        }

        if (dirty_x1 < 0) dirty_x1 = 0;
        if (dirty_y1 < 0) dirty_y1 = 0;
        if (dirty_x2 > (int)srv->fb_info.width)  dirty_x2 = (int)srv->fb_info.width;
        if (dirty_y2 > (int)srv->fb_info.height) dirty_y2 = (int)srv->fb_info.height;
        if (dirty_x1 >= dirty_x2 || dirty_y1 >= dirty_y2)
            continue;

        composite_parallel(srv, &snap, dirty_x1, dirty_y1, dirty_x2, dirty_y2);

        prev_dirty_x = dirty_x1;
        prev_dirty_y = dirty_y1;
        prev_dirty_w = dirty_x2 - dirty_x1;
        prev_dirty_h = dirty_y2 - dirty_y1;
    }

    return NULL;
}
