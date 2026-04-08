/**
 * @file cursor.h
 * @brief 软件鼠标光标
 */

#ifndef WSD_CURSOR_H
#define WSD_CURSOR_H

#include <stdint.h>

struct ws_server;

void cursor_init(struct ws_server *srv);
void cursor_draw_backbuf(struct ws_server *srv, int cx, int cy);

#endif /* WSD_CURSOR_H */
