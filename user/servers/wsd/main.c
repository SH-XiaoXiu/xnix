/**
 * @file main.c
 * @brief ws (Window Server) - 窗口服务器
 *
 * 提供图形窗口管理服务, 通过 IPC 接受客户端请求.
 * 独占 fb_mem, 管理窗口 z-order, 合成 SHM 到 framebuffer.
 */

#include "compositor.h"
#include "cursor.h"
#include "input.h"
#include "window.h"
#include "wsd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xnix/abi/framebuffer.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/protocol/ws.h>
#include <xnix/svc.h>
#include <xnix/sys/server.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

static struct ws_server g_srv;

struct watch_thread_arg {
    struct ws_server *srv;
    uint32_t          slot;
};

static int alloc_client_watch_slot(struct ws_server *srv) {
    for (uint32_t i = 0; i < WS_MAX_WINDOWS; i++) {
        if (!srv->client_watches[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static void *client_watch_thread(void *arg) {
    struct watch_thread_arg *ctx  = arg;
    struct ws_server        *srv  = ctx->srv;
    uint32_t                 slot = ctx->slot;
    handle_t                 notif_handle;
    handle_t                 watch_handle;
    struct ws_window        *victim = NULL;

    free(ctx);

    pthread_mutex_lock(&srv->lock);
    if (!srv->client_watches[slot].used) {
        pthread_mutex_unlock(&srv->lock);
        return NULL;
    }
    notif_handle = srv->client_watches[slot].notif_handle;
    watch_handle = srv->client_watches[slot].watch_handle;
    pthread_mutex_unlock(&srv->lock);

    sys_notification_wait(notif_handle);

    pthread_mutex_lock(&srv->lock);
    for (uint32_t i = 0; i < WS_MAX_WINDOWS; i++) {
        if (srv->windows[i].id != 0 && srv->windows[i].watch_slot == slot) {
            victim = &srv->windows[i];
            break;
        }
    }
    if (victim) {
        window_destroy(srv, victim);
        srv->needs_composite = 1;
        compositor_composite(srv);
    }
    srv->client_watches[slot].used         = 0;
    srv->client_watches[slot].pid          = 0;
    srv->client_watches[slot].notif_handle = HANDLE_INVALID;
    srv->client_watches[slot].watch_handle = HANDLE_INVALID;
    pthread_mutex_unlock(&srv->lock);

    sys_handle_close(watch_handle);
    sys_handle_close(notif_handle);
    return NULL;
}

static int attach_client_watch(struct ws_server *srv, uint32_t pid) {
    int                     slot;
    handle_t                notif_handle;
    handle_t                watch_handle;
    struct watch_thread_arg *ctx;
    pthread_t               tid;

    if (pid == 0) {
        return -1;
    }

    slot = alloc_client_watch_slot(srv);
    if (slot < 0) {
        return -1;
    }

    notif_handle = (handle_t)sys_notification_create();
    if (notif_handle == HANDLE_INVALID) {
        return -1;
    }

    watch_handle = (handle_t)sys_proc_watch((int)pid, notif_handle, 1);
    if (watch_handle == HANDLE_INVALID) {
        sys_handle_close(notif_handle);
        return -1;
    }

    ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        sys_handle_close(watch_handle);
        sys_handle_close(notif_handle);
        return -1;
    }

    srv->client_watches[slot].used         = 1;
    srv->client_watches[slot].pid          = pid;
    srv->client_watches[slot].notif_handle = notif_handle;
    srv->client_watches[slot].watch_handle = watch_handle;

    ctx->srv  = srv;
    ctx->slot = (uint32_t)slot;

    if (pthread_create(&tid, NULL, client_watch_thread, ctx) != 0) {
        srv->client_watches[slot].used         = 0;
        srv->client_watches[slot].pid          = 0;
        srv->client_watches[slot].notif_handle = HANDLE_INVALID;
        srv->client_watches[slot].watch_handle = HANDLE_INVALID;
        sys_handle_close(watch_handle);
        sys_handle_close(notif_handle);
        free(ctx);
        return -1;
    }
    pthread_detach(tid);
    return slot;
}

/**
 * 处理 WS_OP_CREATE_WINDOW
 */
static int handle_create_window(struct ws_server *srv,
                                struct ipc_message *msg) {
    uint32_t w     = msg->regs.data[1];
    uint32_t h     = msg->regs.data[2];
    uint32_t flags = msg->regs.data[3];
    uint32_t pid   = msg->regs.data[4];

    /* 读取标题 */
    char title[WS_TITLE_MAX] = "Window";
    if (msg->buffer.size > 0 && msg->buffer.data != 0) {
        const char *buf = (const char *)(uintptr_t)msg->buffer.data;
        size_t len = msg->buffer.size;
        if (len >= WS_TITLE_MAX)
            len = WS_TITLE_MAX - 1;
        memcpy(title, buf, len);
        title[len] = '\0';
    }



    struct ws_window *win = window_create(srv, w, h, flags, title);
    if (!win) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    win->client_pid = pid;
    win->watch_slot = (uint32_t)attach_client_watch(srv, pid);

    /* 回复: 返回窗口信息和 SHM handle */
    msg->regs.data[0] = 0;
    msg->regs.data[1] = win->id;
    msg->regs.data[2] = (uint32_t)win->client_w;
    msg->regs.data[3] = (uint32_t)win->client_h;
    msg->regs.data[4] = win->shm_size;

    /* 通过回复传递 SHM handle */
    msg->handles.handles[0] = win->shm_handle;
    msg->handles.count = 1;

    srv->needs_composite = 1;
    compositor_composite(srv);


    return 0;
}

/**
 * 处理 WS_OP_DESTROY_WINDOW
 */
static int handle_destroy_window(struct ws_server *srv,
                                 struct ipc_message *msg) {
    uint32_t wid = msg->regs.data[1];



    struct ws_window *win = window_find_by_id(srv, wid);
    if (!win) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    window_destroy(srv, win);
    srv->needs_composite = 1;
    compositor_composite(srv);



    msg->regs.data[0] = 0;
    return 0;
}

/**
 * 处理 WS_OP_SUBMIT
 */
static int handle_submit(struct ws_server *srv, struct ipc_message *msg) {
    uint32_t wid = msg->regs.data[1];
    (void)msg->regs.data[2]; /* x */
    (void)msg->regs.data[3]; /* y */
    (void)msg->regs.data[4]; /* w */
    (void)msg->regs.data[5]; /* h */



    struct ws_window *win = window_find_by_id(srv, wid);
    if (!win) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    /* MVP: 全量重绘 */
    srv->needs_composite = 1;
    compositor_composite(srv);



    msg->regs.data[0] = 0;
    return 0;
}

/**
 * 处理 WS_OP_MOVE_WINDOW
 */
static int handle_move_window(struct ws_server *srv,
                              struct ipc_message *msg) {
    uint32_t wid = msg->regs.data[1];
    int32_t new_x = (int32_t)msg->regs.data[2];
    int32_t new_y = (int32_t)msg->regs.data[3];



    struct ws_window *win = window_find_by_id(srv, wid);
    if (!win) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    win->x = new_x;
    win->y = new_y;
    srv->needs_composite = 1;
    compositor_composite(srv);



    msg->regs.data[0] = 0;
    return 0;
}

/**
 * 处理 WS_OP_SET_TITLE
 */
static int handle_set_title(struct ws_server *srv,
                            struct ipc_message *msg) {
    uint32_t wid = msg->regs.data[1];



    struct ws_window *win = window_find_by_id(srv, wid);
    if (!win) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    if (msg->buffer.size > 0 && msg->buffer.data != 0) {
        const char *buf = (const char *)(uintptr_t)msg->buffer.data;
        size_t len = msg->buffer.size;
        if (len >= WS_TITLE_MAX)
            len = WS_TITLE_MAX - 1;
        memcpy(win->title, buf, len);
        win->title[len] = '\0';
    }

    srv->needs_composite = 1;
    compositor_composite(srv);



    msg->regs.data[0] = 0;
    return 0;
}

/**
 * 处理 WS_OP_GET_SCREEN_INFO
 */
static int handle_get_screen_info(struct ws_server *srv,
                                  struct ipc_message *msg) {
    msg->regs.data[0] = 0;
    msg->regs.data[1] = srv->fb_info.width;
    msg->regs.data[2] = srv->fb_info.height;
    msg->regs.data[3] = srv->fb_info.bpp;
    return 0;
}

static int handle_set_active(struct ws_server *srv,
                             struct ipc_message *msg) {
    srv->active = msg->regs.data[1] ? 1 : 0;
    if (srv->active) {
        srv->first_composite = 1;
        srv->needs_composite = 1;
        compositor_composite(srv);
    }
    msg->regs.data[0] = 0;
    return 0;
}

/**
 * 处理 WS_OP_WAIT_EVENT (延迟回复)
 */
static int handle_wait_event(struct ws_server *srv,
                             struct ipc_message *msg) {
    uint32_t wid = msg->regs.data[1];



    struct ws_window *win = window_find_by_id(srv, wid);
    if (!win) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    /* 检查是否有排队事件 */
    if (win->event_head != win->event_tail) {
        struct ws_queued_event *ev =
            &win->event_ring[win->event_tail];
        win->event_tail = (win->event_tail + 1) % WS_EVENT_RING_SIZE;

        msg->regs.data[0] = ev->type;
        msg->regs.data[1] = win->id;
        for (int i = 0; i < 5; i++)
            msg->regs.data[2 + i] = ev->data[i];

    
        return 0; /* 立即回复 */
    }

    /* 无事件, 延迟回复 */
    if (msg->sender_tid == 0) {
    
        msg->regs.data[0] = (uint32_t)-1;
        return 0;
    }

    win->pending_event_tid = msg->sender_tid;


    return 1; /* 延迟回复 */
}

/**
 * IPC 消息分发
 */
static int ws_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);
    int ret;

    pthread_mutex_lock(&g_srv.lock);

    switch (op) {
    case WS_OP_CREATE_WINDOW:
        ret = handle_create_window(&g_srv, msg);
        break;
    case WS_OP_DESTROY_WINDOW:
        ret = handle_destroy_window(&g_srv, msg);
        break;
    case WS_OP_SUBMIT:
        ret = handle_submit(&g_srv, msg);
        break;
    case WS_OP_MOVE_WINDOW:
        ret = handle_move_window(&g_srv, msg);
        break;
    case WS_OP_SET_TITLE:
        ret = handle_set_title(&g_srv, msg);
        break;
    case WS_OP_GET_SCREEN_INFO:
        ret = handle_get_screen_info(&g_srv, msg);
        break;
    case WS_OP_SET_ACTIVE:
        ret = handle_set_active(&g_srv, msg);
        break;
    case WS_OP_WAIT_EVENT:
        ret = handle_wait_event(&g_srv, msg);
        break;
    default:
        msg->regs.data[0] = (uint32_t)-1;
        ret = 0;
        break;
    }

    pthread_mutex_unlock(&g_srv.lock);
    return ret;
}

/**
 * 初始化 framebuffer
 */
static int init_framebuffer(struct ws_server *srv) {
    handle_t fb_handle = env_require("fb_mem");
    if (fb_handle == HANDLE_INVALID)
        return -1;

    struct physmem_info pinfo = {0};
    if (sys_physmem_info(fb_handle, &pinfo) < 0) {
        ulog_errf("[ws] Failed to get physmem info\n");
        return -1;
    }

    if (pinfo.type != 1) { /* PHYSMEM_TYPE_FB */
        ulog_errf("[ws] fb_mem is not framebuffer type\n");
        return -1;
    }

    srv->fb_info.width      = pinfo.width;
    srv->fb_info.height     = pinfo.height;
    srv->fb_info.pitch      = pinfo.pitch;
    srv->fb_info.bpp        = pinfo.bpp;
    srv->fb_info.red_pos    = pinfo.red_pos;
    srv->fb_info.red_size   = pinfo.red_size;
    srv->fb_info.green_pos  = pinfo.green_pos;
    srv->fb_info.green_size = pinfo.green_size;
    srv->fb_info.blue_pos   = pinfo.blue_pos;
    srv->fb_info.blue_size  = pinfo.blue_size;

    srv->fb_addr = (uint8_t *)sys_mmap_phys(fb_handle, 0, 0, 0x03, NULL);
    if ((int)(uintptr_t)srv->fb_addr < 0 || srv->fb_addr == NULL) {
        ulog_errf("[ws] Failed to map framebuffer\n");
        return -1;
    }

    /* 分配后台缓冲区 (用 SHM 以避免 sbrk 限制) */
    srv->fb_size = srv->fb_info.pitch * srv->fb_info.height;
    handle_t bb_shm = sys_shm_create(srv->fb_size);
    if (bb_shm == (handle_t)-1) {
        ulog_errf("[ws] Failed to create backbuffer SHM (%u bytes)\n",
                  srv->fb_size);
        return -1;
    }
    srv->backbuf = (uint8_t *)sys_mmap_phys(bb_shm, 0, 0, 0x03, NULL);
    if (srv->backbuf == (void *)-1) {
        ulog_errf("[ws] Failed to map backbuffer SHM\n");
        return -1;
    }

    return 0;
}

int main(void) {
    ulog_tagf(stdout, TERM_COLOR_WHITE, "[ws]",
              " Starting window server\n");

    memset(&g_srv, 0, sizeof(g_srv));
    pthread_mutex_init(&g_srv.lock, NULL);

    /* 初始化 framebuffer */
    if (init_framebuffer(&g_srv) < 0)
        return 1;

    compositor_init(&g_srv);

    ulog_tagf(stdout, TERM_COLOR_WHITE, "[ws]",
              " Framebuffer: %ux%u, %u bpp\n",
              g_srv.fb_info.width, g_srv.fb_info.height,
              g_srv.fb_info.bpp);

    /* 获取输入端点 */
    g_srv.kbd_ep = env_require("kbd_ep");
    g_srv.mouse_ep = env_require("mouse_ep");

    if (g_srv.kbd_ep == HANDLE_INVALID ||
        g_srv.mouse_ep == HANDLE_INVALID) {
        ulog_errf("[ws] Missing input endpoints\n");
        return 1;
    }

    /* 获取服务端点 */
    g_srv.ws_ep = env_require("ws_ep");
    if (g_srv.ws_ep == HANDLE_INVALID)
        return 1;

    /* 初始化窗口管理 */
    window_init_all(&g_srv);

    /* 初始化光标，首次合成延后到 GUI session 被激活 */
    cursor_init(&g_srv);
    g_srv.first_composite = 1;

    /* 启动输入线程 */
    input_start_threads(&g_srv);

    /* 启动 IPC 服务 */
    struct sys_server srv = {
        .endpoint = g_srv.ws_ep,
        .handler  = ws_handler,
        .name     = "ws",
    };

    sys_server_init(&srv);
    svc_notify_ready("ws");
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[ws]",
              " Ready, serving on endpoint %u\n", g_srv.ws_ep);

    sys_server_run(&srv);

    return 0;
}
