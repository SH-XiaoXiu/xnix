/**
 * @file window.c
 * @brief 窗口管理实现
 */

#include "window.h"

#include "wsd.h"

#include <string.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

void window_init_all(struct ws_server *srv) {
    memset(srv->windows, 0, sizeof(srv->windows));
    srv->next_window_id = 1;
    srv->z_count        = 0;
    srv->focused_idx    = -1;
}

struct ws_window *window_create(struct ws_server *srv, uint32_t w, uint32_t h, uint32_t flags,
                                const char *title) {
    /* 找空闲槽 */
    struct ws_window *win = NULL;
    uint32_t          idx = 0;
    for (uint32_t i = 0; i < WS_MAX_WINDOWS; i++) {
        if (srv->windows[i].id == 0) {
            win = &srv->windows[i];
            idx = i;
            break;
        }
    }
    if (!win) {
        return NULL;
    }

    /* 限制尺寸 */
    if (w > srv->fb_info.width) {
        w = srv->fb_info.width;
    }
    if (h > srv->fb_info.height) {
        h = srv->fb_info.height;
    }
    if (w == 0) {
        w = 100;
    }
    if (h == 0) {
        h = 100;
    }

    /* 创建 SHM */
    uint32_t shm_size = w * h * 4;
    handle_t shm      = sys_shm_create(shm_size);
    if (shm == (handle_t)-1) {
        ulog_errf("[wsd] shm_create failed for %ux%u\n", w, h);
        return NULL;
    }

    /* 服务器侧映射 */
    uint32_t *shm_addr = (uint32_t *)sys_mmap_phys(shm, 0, 0, 0x03, NULL);
    if (shm_addr == (void *)-1) {
        ulog_errf("[wsd] shm mmap failed\n");
        return NULL;
    }

    /* 清零 SHM (白色背景) */
    memset(shm_addr, 0xFF, shm_size);

    /* 初始化窗口 */
    memset(win, 0, sizeof(*win));
    win->id         = srv->next_window_id++;
    win->watch_slot = UINT32_MAX;
    win->client_w   = (int32_t)w;
    win->client_h   = (int32_t)h;
    win->flags      = flags;
    win->shm_handle = shm;
    win->shm_addr   = shm_addr;
    win->shm_size   = shm_size;

    /* 居中放置 */
    int32_t outer_w = window_outer_w(win);
    int32_t outer_h = window_outer_h(win);
    win->x          = ((int32_t)srv->fb_info.width - outer_w) / 2;
    win->y          = ((int32_t)srv->fb_info.height - outer_h) / 2;

    /* 稍微偏移, 避免完全重叠 */
    int32_t offset = (int32_t)((win->id - 1) % 8) * 30;
    win->x += offset;
    win->y += offset;

    if (title && title[0]) {
        size_t len = strlen(title);
        if (len >= WS_TITLE_MAX) {
            len = WS_TITLE_MAX - 1;
        }
        memcpy(win->title, title, len);
        win->title[len] = '\0';
    } else {
        strcpy(win->title, "Window");
    }

    /* 加入 z-order 顶层 */
    srv->z_order[srv->z_count] = idx;
    srv->z_count++;

    /* 设置焦点 */
    window_set_focus(srv, win);

    return win;
}

void window_destroy(struct ws_server *srv, struct ws_window *win) {
    if (!win || win->id == 0) {
        return;
    }

    window_remove_from_z(srv, win);

    /* 如果是焦点窗口, 转移焦点 */
    if (srv->focused_idx >= 0 && &srv->windows[srv->focused_idx] == win) {
        srv->focused_idx = -1;
        /* 焦点给顶层窗口 */
        if (srv->z_count > 0) {
            uint32_t top_idx = srv->z_order[srv->z_count - 1];
            window_set_focus(srv, &srv->windows[top_idx]);
        }
    }

    win->client_pid = 0;
    win->watch_slot = UINT32_MAX;
    win->pending_event_tid = 0;
    win->id         = 0;
    win->shm_addr   = NULL;
    win->shm_handle = 0;
}

struct ws_window *window_find_by_id(struct ws_server *srv, uint32_t id) {
    if (id == 0) {
        return NULL;
    }
    for (uint32_t i = 0; i < WS_MAX_WINDOWS; i++) {
        if (srv->windows[i].id == id) {
            return &srv->windows[i];
        }
    }
    return NULL;
}

void window_raise_to_top(struct ws_server *srv, struct ws_window *win) {
    /* 找到 win 在 z_order 中的位置 */
    uint32_t win_idx = (uint32_t)(win - srv->windows);
    int      pos     = -1;
    for (uint32_t i = 0; i < srv->z_count; i++) {
        if (srv->z_order[i] == win_idx) {
            pos = (int)i;
            break;
        }
    }
    if (pos < 0) {
        return;
    }

    /* 向后移动填补空位 */
    for (uint32_t i = (uint32_t)pos; i + 1 < srv->z_count; i++) {
        srv->z_order[i] = srv->z_order[i + 1];
    }
    srv->z_order[srv->z_count - 1] = win_idx;
}

void window_remove_from_z(struct ws_server *srv, struct ws_window *win) {
    uint32_t win_idx = (uint32_t)(win - srv->windows);
    int      pos     = -1;
    for (uint32_t i = 0; i < srv->z_count; i++) {
        if (srv->z_order[i] == win_idx) {
            pos = (int)i;
            break;
        }
    }
    if (pos < 0) {
        return;
    }

    for (uint32_t i = (uint32_t)pos; i + 1 < srv->z_count; i++) {
        srv->z_order[i] = srv->z_order[i + 1];
    }
    srv->z_count--;
}

void window_set_focus(struct ws_server *srv, struct ws_window *win) {
    /* 取消旧焦点 */
    if (srv->focused_idx >= 0) {
        struct ws_window *old = &srv->windows[srv->focused_idx];
        if (old->id != 0 && old != win) {
            old->focused     = 0;
            uint32_t data[5] = {0};
            data[0]          = 0; /* lost */
            window_push_event(old, WS_EVENT_FOCUS, data);
        }
    }

    if (win && win->id != 0) {
        win->focused     = 1;
        srv->focused_idx = (int32_t)(win - srv->windows);
        uint32_t data[5] = {0};
        data[0]          = 1; /* gained */
        window_push_event(win, WS_EVENT_FOCUS, data);
    } else {
        srv->focused_idx = -1;
    }
}

void window_push_event(struct ws_window *win, uint32_t type, const uint32_t *data) {
    if (!win || win->id == 0) {
        return;
    }

    /* 如果客户端正在等待, 立即回复 */
    if (win->pending_event_tid != 0) {
        struct ipc_message reply;
        memset(&reply, 0, sizeof(reply));
        reply.regs.data[0] = type;
        reply.regs.data[1] = win->id;
        if (data) {
            for (int i = 0; i < 5; i++) {
                reply.regs.data[2 + i] = data[i];
            }
        }

        sys_ipc_reply_to(win->pending_event_tid, &reply);
        win->pending_event_tid = 0;
        return;
    }

    /* 入队 */
    uint16_t next = (win->event_head + 1) % WS_EVENT_RING_SIZE;
    if (next == win->event_tail) {
        /* 满了, 丢弃最旧的 */
        win->event_tail = (win->event_tail + 1) % WS_EVENT_RING_SIZE;
    }

    struct ws_queued_event *ev = &win->event_ring[win->event_head];
    ev->type                   = type;
    if (data) {
        for (int i = 0; i < 5; i++) {
            ev->data[i] = data[i];
        }
    } else {
        memset(ev->data, 0, sizeof(ev->data));
    }
    win->event_head = next;
}

int window_find_at(struct ws_server *srv, int32_t px, int32_t py) {
    /* 从顶层往下搜索 */
    for (int i = (int)srv->z_count - 1; i >= 0; i--) {
        uint32_t          idx = srv->z_order[i];
        struct ws_window *w   = &srv->windows[idx];
        if (w->id == 0) {
            continue;
        }

        int32_t ow = window_outer_w(w);
        int32_t oh = window_outer_h(w);

        if (px >= w->x && px < w->x + ow && py >= w->y && py < w->y + oh) {
            return (int)idx;
        }
    }
    return -1;
}

int window_hit_titlebar(struct ws_window *win, int32_t px, int32_t py) {
    if (win->flags & WS_FLAG_NO_DECOR) {
        return 0;
    }

    int32_t tb_x = win->x + WS_BORDER_WIDTH;
    int32_t tb_y = win->y + WS_BORDER_WIDTH;
    int32_t tb_w = win->client_w;

    return (px >= tb_x && px < tb_x + tb_w && py >= tb_y && py < tb_y + WS_TITLEBAR_HEIGHT);
}

int window_hit_close_btn(struct ws_window *win, int32_t px, int32_t py) {
    if (win->flags & WS_FLAG_NO_DECOR) {
        return 0;
    }

    int32_t total_inner_w = win->client_w;
    int32_t close_x       = win->x + WS_BORDER_WIDTH + total_inner_w - WS_CLOSE_BTN_SIZE - 4;
    int32_t close_y       = win->y + WS_BORDER_WIDTH + (WS_TITLEBAR_HEIGHT - WS_CLOSE_BTN_SIZE) / 2;

    return (px >= close_x && px < close_x + WS_CLOSE_BTN_SIZE && py >= close_y &&
            py < close_y + WS_CLOSE_BTN_SIZE);
}

int window_hit_client(struct ws_window *win, int32_t px, int32_t py) {
    int32_t cx = window_client_x(win);
    int32_t cy = window_client_y(win);

    return (px >= cx && px < cx + win->client_w && py >= cy && py < cy + win->client_h);
}
