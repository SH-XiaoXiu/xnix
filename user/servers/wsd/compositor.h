/**
 * @file compositor.h
 * @brief 合成器
 */

#ifndef WSD_COMPOSITOR_H
#define WSD_COMPOSITOR_H

#include <stdint.h>

struct ws_server;
struct ws_window;

void compositor_init(struct ws_server *srv);
void compositor_composite(struct ws_server *srv);

/* 低级 framebuffer 操作 */
/* 后台缓冲区操作 (合成用) */
void fb_put_pixel(struct ws_server *srv, int x, int y, uint32_t color);
uint32_t fb_get_pixel(struct ws_server *srv, int x, int y);
void fb_fill_rect(struct ws_server *srv, int x, int y, int w, int h, uint32_t color);
uint32_t fb_make_color(struct ws_server *srv, uint8_t r, uint8_t g, uint8_t b);

/* 前台 framebuffer 操作 (光标局部更新用) */
void fb_put_pixel_front(struct ws_server *srv, int x, int y, uint32_t color);
uint32_t fb_get_pixel_front(struct ws_server *srv, int x, int y);

#endif /* WSD_COMPOSITOR_H */
