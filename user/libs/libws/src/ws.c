/**
 * @file ws.c
 * @brief 窗口服务器客户端库实现
 */

#include <ws/ws.h>

#include <font/font.h>
#include <stdlib.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/protocol/input.h>
#include <xnix/protocol/ws.h>
#include <xnix/syscall.h>

struct ws_window {
    uint32_t  id;
    uint32_t  width;
    uint32_t  height;
    handle_t  shm_handle;
    uint32_t *pixels;
    uint32_t  shm_size;
};

static handle_t g_ws_ep = HANDLE_INVALID;

int ws_connect(void) {
    g_ws_ep = env_require("ws_ep");
    if (g_ws_ep == HANDLE_INVALID)
        return -1;
    return 0;
}

ws_window_t *ws_create_window(uint32_t w, uint32_t h, const char *title) {
    if (g_ws_ep == HANDLE_INVALID)
        return NULL;

    struct ipc_message req, reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = WS_OP_CREATE_WINDOW;
    req.regs.data[1] = w;
    req.regs.data[2] = h;
    req.regs.data[3] = 0; /* flags */

    /* 传递标题通过 buffer */
    char title_buf[WS_TITLE_MAX];
    if (title) {
        size_t len = strlen(title);
        if (len >= WS_TITLE_MAX)
            len = WS_TITLE_MAX - 1;
        memcpy(title_buf, title, len);
        title_buf[len] = '\0';
        IPC_BUF_SET_PTR(&req.buffer, title_buf);
        req.buffer.size = (uint32_t)len + 1;
    }

    if (sys_ipc_call(g_ws_ep, &req, &reply, 0) < 0)
        return NULL;

    if (reply.regs.data[0] != 0)
        return NULL;

    uint32_t win_id = reply.regs.data[1];
    uint32_t actual_w = reply.regs.data[2];
    uint32_t actual_h = reply.regs.data[3];
    uint32_t shm_size = reply.regs.data[4];

    /* 获取 SHM handle (由服务器通过回复传递) */
    if (reply.handles.count == 0)
        return NULL;

    handle_t shm = reply.handles.handles[0];

    /* 映射 SHM */
    uint32_t *pixels = (uint32_t *)sys_mmap_phys(shm, 0, 0, 0x03, NULL);
    if (pixels == (void *)-1)
        return NULL;

    ws_window_t *win = (ws_window_t *)malloc(sizeof(ws_window_t));
    if (!win)
        return NULL;

    win->id = win_id;
    win->width = actual_w;
    win->height = actual_h;
    win->shm_handle = shm;
    win->pixels = pixels;
    win->shm_size = shm_size;

    return win;
}

void ws_destroy_window(ws_window_t *win) {
    if (!win)
        return;

    if (g_ws_ep != HANDLE_INVALID) {
        struct ipc_message req, reply;
        memset(&req, 0, sizeof(req));
        memset(&reply, 0, sizeof(reply));

        req.regs.data[0] = WS_OP_DESTROY_WINDOW;
        req.regs.data[1] = win->id;

        sys_ipc_call(g_ws_ep, &req, &reply, 0);
    }

    free(win);
}

uint32_t *ws_get_buffer(ws_window_t *win) {
    return win ? win->pixels : NULL;
}

void ws_get_size(ws_window_t *win, uint32_t *w, uint32_t *h) {
    if (!win) return;
    if (w) *w = win->width;
    if (h) *h = win->height;
}

uint32_t ws_get_id(ws_window_t *win) {
    return win ? win->id : 0;
}

int ws_submit(ws_window_t *win, uint32_t x, uint32_t y,
              uint32_t w, uint32_t h) {
    if (!win || g_ws_ep == HANDLE_INVALID)
        return -1;

    struct ipc_message req, reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = WS_OP_SUBMIT;
    req.regs.data[1] = win->id;
    req.regs.data[2] = x;
    req.regs.data[3] = y;
    req.regs.data[4] = w;
    req.regs.data[5] = h;

    if (sys_ipc_call(g_ws_ep, &req, &reply, 0) < 0)
        return -1;

    return (reply.regs.data[0] == 0) ? 0 : -1;
}

int ws_submit_all(ws_window_t *win) {
    if (!win) return -1;
    return ws_submit(win, 0, 0, win->width, win->height);
}

int ws_wait_event(ws_window_t *win, struct ws_event *ev) {
    if (!win || !ev || g_ws_ep == HANDLE_INVALID)
        return -1;

    struct ipc_message req, reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = WS_OP_WAIT_EVENT;
    req.regs.data[1] = win->id;

    if (sys_ipc_call(g_ws_ep, &req, &reply, 0) < 0)
        return -1;

    if (reply.regs.data[0] == (uint32_t)-1)
        return -1;

    memset(ev, 0, sizeof(*ev));
    ev->type = (uint8_t)reply.regs.data[0];
    ev->window_id = reply.regs.data[1];

    switch (ev->type) {
    case WS_EV_INPUT: {
        uint32_t reg1 = reply.regs.data[2];
        uint32_t reg2 = reply.regs.data[3];
        uint32_t reg3 = reply.regs.data[4];

        ev->input.type      = INPUT_UNPACK_TYPE(reg1);
        ev->input.modifiers = INPUT_UNPACK_MODIFIERS(reg1);
        ev->input.code      = INPUT_UNPACK_CODE(reg1);
        ev->input.value     = INPUT_UNPACK_VALUE(reg2);
        ev->input.value2    = INPUT_UNPACK_VALUE2(reg2);
        ev->input.timestamp = INPUT_UNPACK_TIMESTAMP(reg3);
        break;
    }
    case WS_EV_FOCUS:
        ev->focused = (uint8_t)reply.regs.data[2];
        break;
    case WS_EV_CLOSE:
        break;
    }

    return 0;
}

int ws_get_screen_info(uint32_t *w, uint32_t *h) {
    if (g_ws_ep == HANDLE_INVALID)
        return -1;

    struct ipc_message req, reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = WS_OP_GET_SCREEN_INFO;

    if (sys_ipc_call(g_ws_ep, &req, &reply, 0) < 0)
        return -1;

    if (reply.regs.data[0] != 0)
        return -1;

    if (w) *w = reply.regs.data[1];
    if (h) *h = reply.regs.data[2];
    return 0;
}

/*
 * 本地绘图 (直接操作 SHM)
 */

void ws_fill_rect(ws_window_t *win, uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h, uint32_t argb) {
    if (!win || !win->pixels)
        return;

    /* 裁剪 */
    if (x >= win->width || y >= win->height)
        return;
    if (x + w > win->width) w = win->width - x;
    if (y + h > win->height) h = win->height - y;

    for (uint32_t row = y; row < y + h; row++) {
        uint32_t *line = &win->pixels[row * win->width + x];
        for (uint32_t col = 0; col < w; col++)
            line[col] = argb;
    }
}

void ws_draw_char(ws_window_t *win, uint32_t x, uint32_t y,
                  char c, uint32_t fg, uint32_t bg) {
    if (!win || !win->pixels)
        return;

    const uint8_t *glyph = font_get_ascii_8x16((uint32_t)(uint8_t)c);
    if (!glyph)
        return;

    for (uint32_t row = 0; row < FONT_ASCII_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        uint32_t py = y + row;
        if (py >= win->height) break;

        for (uint32_t col = 0; col < FONT_ASCII_WIDTH; col++) {
            uint32_t px = x + col;
            if (px >= win->width) break;

            uint32_t color = (bits & (1u << (7 - col))) ? fg : bg;
            win->pixels[py * win->width + px] = color;
        }
    }
}

void ws_draw_text(ws_window_t *win, uint32_t x, uint32_t y,
                  const char *text, uint32_t fg, uint32_t bg) {
    if (!text)
        return;

    for (int i = 0; text[i]; i++) {
        ws_draw_char(win, x + (uint32_t)i * FONT_ASCII_WIDTH, y,
                     text[i], fg, bg);
    }
}
