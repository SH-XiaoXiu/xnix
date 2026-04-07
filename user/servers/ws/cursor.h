/**
 * @file cursor.h
 * @brief 软件鼠标光标
 */

#ifndef WSD_CURSOR_H
#define WSD_CURSOR_H

#include <stdint.h>

struct ws_server;

void cursor_init(struct ws_server *srv);
void cursor_save(struct ws_server *srv);
void cursor_restore(struct ws_server *srv);
void cursor_draw(struct ws_server *srv);          /* 画到前台 fb (局部更新) */
void cursor_draw_backbuf(struct ws_server *srv);  /* 画到后台缓冲区 (合成时) */

#endif /* WSD_CURSOR_H */
