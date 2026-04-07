/**
 * @file input.h
 * @brief 输入线程和路由
 */

#ifndef WSD_INPUT_H
#define WSD_INPUT_H

#include <xnix/abi/input.h>

struct ws_server;

/**
 * 启动 kbd/mouse 输入线程
 */
void input_start_threads(struct ws_server *srv);

void route_mouse_event(struct ws_server *srv, struct input_event *ev);
void route_kbd_event(struct ws_server *srv, struct input_event *ev);

#endif /* WSD_INPUT_H */
