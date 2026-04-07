/**
 * @file input.c
 * @brief 输入线程和路由
 */

#include "input.h"
#include "compositor.h"
#include "cursor.h"
#include "window.h"
#include "wsd.h"

#include <pthread.h>
#include <string.h>
#include <xnix/abi/input.h>
#include <xnix/ipc.h>
#include <xnix/protocol/input.h>
#include <xnix/protocol/inputdev.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/**
 * 从 IPC 回复中解码 input_event
 */
static void decode_input_event(struct ipc_message *reply,
                               struct input_event *ev) {
    uint32_t reg1 = reply->regs.data[1];
    uint32_t reg2 = reply->regs.data[2];
    uint32_t reg3 = reply->regs.data[3];

    ev->type      = INPUT_UNPACK_TYPE(reg1);
    ev->modifiers = INPUT_UNPACK_MODIFIERS(reg1);
    ev->code      = INPUT_UNPACK_CODE(reg1);
    ev->value     = INPUT_UNPACK_VALUE(reg2);
    ev->value2    = INPUT_UNPACK_VALUE2(reg2);
    ev->timestamp = INPUT_UNPACK_TIMESTAMP(reg3);
    ev->_reserved = 0;
}

static int inputdev_read_event(handle_t ep, struct input_event *ev) {
    struct ipc_message req, reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = INPUTDEV_READ;

    if (sys_ipc_call(ep, &req, &reply, 0) < 0) {
        return -1;
    }
    if ((int32_t)reply.regs.data[0] < 0) {
        return -1;
    }

    decode_input_event(&reply, ev);
    return 0;
}

/**
 * 将 input_event 打包为窗口事件数据
 */
static void pack_input_data(const struct input_event *ev, uint32_t *data) {
    data[0] = INPUT_PACK_REG1(ev);
    data[1] = INPUT_PACK_REG2(ev);
    data[2] = INPUT_PACK_REG3(ev);
    data[3] = 0;
    data[4] = 0;
}

/**
 * 路由鼠标事件
 */
void route_mouse_event(struct ws_server *srv, struct input_event *ev) {
    if (ev->type == INPUT_EVENT_MOUSE_MOVE) {
        srv->cursor_x += ev->value;
        srv->cursor_y += ev->value2;

        if (srv->cursor_x < 0) srv->cursor_x = 0;
        if (srv->cursor_y < 0) srv->cursor_y = 0;
        if (srv->cursor_x >= (int32_t)srv->fb_info.width)
            srv->cursor_x = (int32_t)srv->fb_info.width - 1;
        if (srv->cursor_y >= (int32_t)srv->fb_info.height)
            srv->cursor_y = (int32_t)srv->fb_info.height - 1;

        if (srv->dragging) {
            struct ws_window *win = &srv->windows[srv->drag_win_idx];
            if (win->id != 0) {
                win->x = srv->cursor_x - srv->drag_off_x;
                win->y = srv->cursor_y - srv->drag_off_y;
            }
            /* 拖拽窗口需要全量合成 */
            srv->needs_composite = 1;
        } else {
            /* 纯光标移动: 局部更新,不触发全量合成 */
            cursor_restore(srv);
            cursor_save(srv);
            cursor_draw(srv);
        }

        /* MOUSE_MOVE 不转发给客户端窗口, 避免每次移动触发全屏合成 */
    } else if (ev->type == INPUT_EVENT_MOUSE_BUTTON) {
        int left_btn = (ev->code == INPUT_MOUSE_BTN_LEFT);
        int pressed = (ev->value == 1);

        if (left_btn && pressed) {
            int win_idx = window_find_at(srv, srv->cursor_x, srv->cursor_y);
            if (win_idx >= 0) {
                struct ws_window *win = &srv->windows[win_idx];

                if (window_hit_close_btn(win, srv->cursor_x, srv->cursor_y)) {
                    window_push_event(win, WS_EVENT_CLOSE, NULL);
                } else if (window_hit_titlebar(win, srv->cursor_x,
                                               srv->cursor_y)) {
                    srv->dragging = 1;
                    srv->drag_win_idx = (uint32_t)win_idx;
                    srv->drag_off_x = srv->cursor_x - win->x;
                    srv->drag_off_y = srv->cursor_y - win->y;
                    window_raise_to_top(srv, win);
                    window_set_focus(srv, win);
                } else {
                    window_raise_to_top(srv, win);
                    window_set_focus(srv, win);
                    struct input_event client_ev = *ev;
                    client_ev.value = (int16_t)(srv->cursor_x -
                                     window_client_x(win));
                    client_ev.value2 = (int16_t)(srv->cursor_y -
                                      window_client_y(win));
                    uint32_t data[5];
                    pack_input_data(&client_ev, data);
                    window_push_event(win, WS_EVENT_INPUT, data);
                }
                srv->needs_composite = 1;
            }
        } else if (left_btn && !pressed) {
            if (srv->dragging) {
                srv->dragging = 0;
                srv->needs_composite = 1;
            }
            if (srv->focused_idx >= 0) {
                struct ws_window *fw = &srv->windows[srv->focused_idx];
                if (fw->id != 0) {
                    uint32_t data[5];
                    pack_input_data(ev, data);
                    window_push_event(fw, WS_EVENT_INPUT, data);
                }
            }
        } else {
            if (srv->focused_idx >= 0) {
                struct ws_window *fw = &srv->windows[srv->focused_idx];
                if (fw->id != 0) {
                    uint32_t data[5];
                    pack_input_data(ev, data);
                    window_push_event(fw, WS_EVENT_INPUT, data);
                }
            }
        }
    }
}

/**
 * 路由键盘事件
 */
void route_kbd_event(struct ws_server *srv, struct input_event *ev) {
    if (srv->focused_idx < 0)
        return;
    struct ws_window *win = &srv->windows[srv->focused_idx];
    if (win->id == 0)
        return;
    uint32_t data[5];
    pack_input_data(ev, data);
    window_push_event(win, WS_EVENT_INPUT, data);
}

/**
 * 键盘输入线程
 */
static void *kbd_thread(void *arg) {
    struct ws_server *srv = (struct ws_server *)arg;

    while (1) {
        if (!srv->active) {
            sys_sleep(20);
            continue;
        }

        struct input_event ev;
        if (inputdev_read_event(srv->kbd_ep, &ev) < 0) {
            sys_sleep(100);
            continue;
        }

        pthread_mutex_lock(&srv->lock);
        route_kbd_event(srv, &ev);
        if (srv->needs_composite)
            compositor_composite(srv);
        pthread_mutex_unlock(&srv->lock);
    }

    return NULL;
}

/**
 * 从 mouse_ep 读取一个事件 (非阻塞检查 + 阻塞读)
 * @return 0 成功, -1 失败
 */
static int mouse_read_event(struct ws_server *srv, struct input_event *ev) {
    return inputdev_read_event(srv->mouse_ep, ev);
}

/**
 * 检查 mouse_ep 是否有更多事件
 */
static int mouse_has_events(struct ws_server *srv) {
    struct ipc_message req, reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));
    req.regs.data[0] = INPUTDEV_POLL;

    if (sys_ipc_call(srv->mouse_ep, &req, &reply, 0) < 0)
        return 0;
    if ((int32_t)reply.regs.data[0] < 0)
        return 0;
    return reply.regs.data[1] != 0;
}

static void route_coalesced_mouse_move(struct ws_server *srv, int16_t dx, int16_t dy) {
    if (dx == 0 && dy == 0) {
        return;
    }

    struct input_event ev = {
        .type      = INPUT_EVENT_MOUSE_MOVE,
        .modifiers = 0,
        .code      = 0,
        .value     = dx,
        .value2    = dy,
        .timestamp = 0,
        ._reserved = 0,
    };
    route_mouse_event(srv, &ev);
}

/**
 * 鼠标输入线程
 */
static void *mouse_thread(void *arg) {
    struct ws_server *srv = (struct ws_server *)arg;

    while (1) {
        if (!srv->active) {
            sys_sleep(20);
            continue;
        }

        struct input_event ev;

        /* 阻塞等待第一个事件 */
        if (mouse_read_event(srv, &ev) < 0) {
            sys_sleep(100);
            continue;
        }

        pthread_mutex_lock(&srv->lock);

        /* 处理第一个事件 */
        int32_t pending_dx = 0;
        int32_t pending_dy = 0;
        if (ev.type == INPUT_EVENT_MOUSE_MOVE) {
            pending_dx += ev.value;
            pending_dy += ev.value2;
        } else {
            route_mouse_event(srv, &ev);
        }

        /* 批量处理: 合并连续鼠标移动，保留按钮事件顺序 */
        while (mouse_has_events(srv)) {
            if (mouse_read_event(srv, &ev) < 0)
                break;

            if (ev.type == INPUT_EVENT_MOUSE_MOVE) {
                pending_dx += ev.value;
                pending_dy += ev.value2;
                continue;
            }

            route_coalesced_mouse_move(srv, (int16_t)pending_dx, (int16_t)pending_dy);
            pending_dx = 0;
            pending_dy = 0;
            route_mouse_event(srv, &ev);
        }

        route_coalesced_mouse_move(srv, (int16_t)pending_dx, (int16_t)pending_dy);

        if (srv->needs_composite)
            compositor_composite(srv);

        pthread_mutex_unlock(&srv->lock);
    }

    return NULL;
}

void input_start_threads(struct ws_server *srv) {
    pthread_t tid;

    if (pthread_create(&tid, NULL, kbd_thread, srv) != 0) {
        ulog_errf("[ws] Failed to create kbd thread\n");
    }

    if (pthread_create(&tid, NULL, mouse_thread, srv) != 0) {
        ulog_errf("[ws] Failed to create mouse thread\n");
    }
}
