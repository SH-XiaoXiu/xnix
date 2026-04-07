/**
 * @file cursor.c
 * @brief 软件鼠标光标实现
 *
 * 光标局部更新直接操作前台 framebuffer (避免全屏合成).
 * 全量合成时光标画到后台缓冲区, 由 compositor 统一翻转.
 */

#include "cursor.h"
#include "compositor.h"
#include "wsd.h"

/**
 * 12x19 箭头光标位图
 * 1=白色前景, 2=黑色边框, 0=透明
 */
static const uint8_t cursor_bitmap[WS_CURSOR_H][WS_CURSOR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,1,1,1,1,2,0},
    {2,1,1,1,1,1,1,1,1,1,1,2},
    {2,1,1,1,1,1,1,2,2,2,2,2},
    {2,1,1,1,2,1,1,2,0,0,0,0},
    {2,1,1,2,0,2,1,1,2,0,0,0},
    {2,1,2,0,0,2,1,1,2,0,0,0},
    {2,2,0,0,0,0,2,1,1,2,0,0},
    {2,0,0,0,0,0,2,1,1,2,0,0},
    {0,0,0,0,0,0,0,2,2,0,0,0},
};

void cursor_init(struct ws_server *srv) {
    srv->cursor_x = (int32_t)srv->fb_info.width / 2;
    srv->cursor_y = (int32_t)srv->fb_info.height / 2;
    srv->cursor_visible = 0;
    srv->cursor_saved_x = 0;
    srv->cursor_saved_y = 0;
}

/**
 * 保存前台 framebuffer 上光标区域的像素
 */
void cursor_save(struct ws_server *srv) {
    int cx = srv->cursor_x;
    int cy = srv->cursor_y;
    int idx = 0;

    for (int row = 0; row < WS_CURSOR_H; row++) {
        int sy = cy + row;
        for (int col = 0; col < WS_CURSOR_W; col++) {
            int sx = cx + col;
            if (sx >= 0 && (uint32_t)sx < srv->fb_info.width &&
                sy >= 0 && (uint32_t)sy < srv->fb_info.height) {
                srv->cursor_saved[idx] = fb_get_pixel_front(srv, sx, sy);
            }
            idx++;
        }
    }

    srv->cursor_saved_x = cx;
    srv->cursor_saved_y = cy;
    srv->cursor_visible = 1;
}

/**
 * 恢复前台 framebuffer 上光标区域的像素
 */
void cursor_restore(struct ws_server *srv) {
    if (!srv->cursor_visible)
        return;

    int cx = srv->cursor_saved_x;
    int cy = srv->cursor_saved_y;
    int idx = 0;

    for (int row = 0; row < WS_CURSOR_H; row++) {
        int sy = cy + row;
        for (int col = 0; col < WS_CURSOR_W; col++) {
            int sx = cx + col;
            if (sx >= 0 && (uint32_t)sx < srv->fb_info.width &&
                sy >= 0 && (uint32_t)sy < srv->fb_info.height) {
                fb_put_pixel_front(srv, sx, sy, srv->cursor_saved[idx]);
            }
            idx++;
        }
    }

    srv->cursor_visible = 0;
}

/**
 * 绘制光标到前台 framebuffer (局部更新用)
 */
void cursor_draw(struct ws_server *srv) {
    int cx = srv->cursor_x;
    int cy = srv->cursor_y;
    uint32_t white = fb_make_color(srv, 0xFF, 0xFF, 0xFF);
    uint32_t black = fb_make_color(srv, 0x00, 0x00, 0x00);

    for (int row = 0; row < WS_CURSOR_H; row++) {
        int sy = cy + row;
        if (sy < 0 || (uint32_t)sy >= srv->fb_info.height)
            continue;
        for (int col = 0; col < WS_CURSOR_W; col++) {
            int sx = cx + col;
            if (sx < 0 || (uint32_t)sx >= srv->fb_info.width)
                continue;
            uint8_t v = cursor_bitmap[row][col];
            if (v == 1)
                fb_put_pixel_front(srv, sx, sy, white);
            else if (v == 2)
                fb_put_pixel_front(srv, sx, sy, black);
        }
    }
}

/**
 * 绘制光标到后台缓冲区 (全量合成用)
 */
void cursor_draw_backbuf(struct ws_server *srv) {
    int cx = srv->cursor_x;
    int cy = srv->cursor_y;
    uint32_t white = fb_make_color(srv, 0xFF, 0xFF, 0xFF);
    uint32_t black = fb_make_color(srv, 0x00, 0x00, 0x00);

    for (int row = 0; row < WS_CURSOR_H; row++) {
        int sy = cy + row;
        if (sy < 0 || (uint32_t)sy >= srv->fb_info.height)
            continue;
        for (int col = 0; col < WS_CURSOR_W; col++) {
            int sx = cx + col;
            if (sx < 0 || (uint32_t)sx >= srv->fb_info.width)
                continue;
            uint8_t v = cursor_bitmap[row][col];
            if (v == 1)
                fb_put_pixel(srv, sx, sy, white);
            else if (v == 2)
                fb_put_pixel(srv, sx, sy, black);
        }
    }
}
