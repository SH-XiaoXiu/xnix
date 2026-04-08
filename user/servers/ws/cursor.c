/**
 * @file cursor.c
 * @brief 软件鼠标光标实现
 *
 * 光标只绘制到后台缓冲区, 由 compositor render 线程统一合成翻转.
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
}

/**
 * 绘制光标到后台缓冲区 (合成时由 render 线程调用)
 */
void cursor_draw_backbuf(struct ws_server *srv, int cx, int cy) {
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
