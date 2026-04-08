/**
 * @file wsd.h
 * @brief 窗口服务器全局状态
 */

#ifndef WSD_WSD_H
#define WSD_WSD_H

#include <pthread.h>
#include <xnix/abi/framebuffer.h>
#include <xnix/abi/handle.h>
#include <xnix/protocol/ws.h>
#include "window.h"

#define WS_CURSOR_W 12
#define WS_CURSOR_H 19

struct ws_server {
    /* Framebuffer */
    uint8_t           *fb_addr;
    uint8_t           *backbuf;     /* 后台缓冲区 (与 fb 同大小) */
    uint32_t           fb_size;     /* framebuffer 总字节数 */
    struct abi_fb_info fb_info;
    uint8_t            fb_bpp_bytes;
    uint8_t            fb_is_argb32; /* fb 格式与 ARGB32 兼容, 可直接 memcpy */

    /* 窗口 */
    struct ws_window windows[WS_MAX_WINDOWS];
    uint32_t         next_window_id;

    /* Z-order: z_order[0]=底层, z_order[z_count-1]=顶层 */
    uint32_t z_order[WS_MAX_WINDOWS];
    uint32_t z_count;
    int32_t  focused_idx; /* windows[] 索引, -1=无焦点 */

    /* 鼠标光标 */
    int32_t  cursor_x, cursor_y;

    /* 拖拽 */
    uint8_t  dragging;
    uint32_t drag_win_idx;
    int32_t  drag_off_x, drag_off_y;

    /* IPC 端点 */
    handle_t ws_ep;
    handle_t kbd_ep;
    handle_t mouse_ep;

    /* session state */
    uint8_t active;

    struct {
        uint8_t  used;
        uint32_t pid;
        handle_t notif_handle;
        handle_t watch_handle;
    } client_watches[WS_MAX_WINDOWS];

    /* 合成标记 */
    uint8_t needs_composite;
    uint8_t first_composite;

    /* 同步 (输入线程与主线程) */
    pthread_mutex_t lock;

    /* 渲染线程 */
    handle_t        render_event;
    pthread_t       render_tid;
};

#endif /* WSD_WSD_H */
