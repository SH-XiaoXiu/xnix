/**
 * @file window.h
 * @brief 窗口管理
 */

#ifndef WSD_WINDOW_H
#define WSD_WINDOW_H

#include <stdint.h>
#include <xnix/abi/handle.h>
#include <xnix/protocol/ws.h>

struct ws_server;

/**
 * 排队事件 (当客户端无挂起的 WAIT_EVENT 时缓存)
 */
struct ws_queued_event {
    uint32_t type;    /* ws_event_type */
    uint32_t data[5]; /* 寄存器数据 */
};

#define WS_EVENT_RING_SIZE 16

/**
 * 窗口结构
 */
struct ws_window {
    uint32_t id;                /* 1-based, 0=空闲 */
    uint32_t client_pid;

    /* 几何 (屏幕坐标, 外框左上角) */
    int32_t x, y;
    int32_t client_w, client_h;
    uint32_t flags;

    /* SHM (客户端像素数据, ARGB32) */
    handle_t shm_handle;
    uint32_t *shm_addr;
    uint32_t shm_size;

    /* 事件投递 */
    uint32_t pending_event_tid;
    struct ws_queued_event event_ring[WS_EVENT_RING_SIZE];
    uint16_t event_head, event_tail;

    char title[WS_TITLE_MAX];
    uint8_t focused;
};

void window_init_all(struct ws_server *srv);

struct ws_window *window_create(struct ws_server *srv, uint32_t w, uint32_t h,
                                uint32_t flags, const char *title);
void window_destroy(struct ws_server *srv, struct ws_window *win);

struct ws_window *window_find_by_id(struct ws_server *srv, uint32_t id);

/**
 * z-order 管理
 */
void window_raise_to_top(struct ws_server *srv, struct ws_window *win);
void window_remove_from_z(struct ws_server *srv, struct ws_window *win);

/**
 * 焦点管理
 */
void window_set_focus(struct ws_server *srv, struct ws_window *win);

/**
 * 事件推送
 */
void window_push_event(struct ws_window *win, uint32_t type, const uint32_t *data);

/**
 * 命中测试
 */
int window_find_at(struct ws_server *srv, int32_t px, int32_t py);
int window_hit_titlebar(struct ws_window *win, int32_t px, int32_t py);
int window_hit_close_btn(struct ws_window *win, int32_t px, int32_t py);
int window_hit_client(struct ws_window *win, int32_t px, int32_t py);

/**
 * 获取窗口外框尺寸
 */
static inline int32_t window_outer_w(struct ws_window *win) {
    if (win->flags & WS_FLAG_NO_DECOR)
        return win->client_w;
    return win->client_w + 2 * WS_BORDER_WIDTH;
}

static inline int32_t window_outer_h(struct ws_window *win) {
    if (win->flags & WS_FLAG_NO_DECOR)
        return win->client_h;
    return win->client_h + WS_TITLEBAR_HEIGHT + 2 * WS_BORDER_WIDTH;
}

static inline int32_t window_client_x(struct ws_window *win) {
    if (win->flags & WS_FLAG_NO_DECOR)
        return win->x;
    return win->x + WS_BORDER_WIDTH;
}

static inline int32_t window_client_y(struct ws_window *win) {
    if (win->flags & WS_FLAG_NO_DECOR)
        return win->y;
    return win->y + WS_BORDER_WIDTH + WS_TITLEBAR_HEIGHT;
}

#endif /* WSD_WINDOW_H */
