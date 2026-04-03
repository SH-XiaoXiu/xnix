/**
 * @file ws/ws.h
 * @brief 窗口服务器客户端库
 *
 * 封装 wsd IPC 协议, 提供简洁的窗口操作和绘图 API.
 */

#ifndef WS_WS_H
#define WS_WS_H

#include <stdint.h>
#include <xnix/abi/input.h>

/**
 * 窗口句柄 (不透明指针)
 */
typedef struct ws_window ws_window_t;

/**
 * 事件类型
 */
enum ws_event_type_e {
    WS_EV_INPUT = 1,
    WS_EV_FOCUS = 2,
    WS_EV_CLOSE = 3,
};

/**
 * 客户端事件
 */
struct ws_event {
    uint8_t            type;      /* ws_event_type_e */
    uint32_t           window_id;
    struct input_event input;     /* 仅 type==WS_EV_INPUT 有效 */
    uint8_t            focused;   /* 仅 type==WS_EV_FOCUS 有效: 1=得到/0=失去 */
};

/**
 * 连接到窗口服务器
 *
 * 查找 "ws_ep" handle. 必须在调用其他 ws_* 函数之前调用.
 * @return 0 成功, -1 失败
 */
int ws_connect(void);

/**
 * 创建窗口
 *
 * @param w     客户区宽度 (像素)
 * @param h     客户区高度 (像素)
 * @param title 窗口标题 (UTF-8, max 63 字节)
 * @return 窗口指针, NULL 失败
 */
ws_window_t *ws_create_window(uint32_t w, uint32_t h, const char *title);

/**
 * 销毁窗口
 */
void ws_destroy_window(ws_window_t *win);

/**
 * 获取窗口像素缓冲区
 *
 * 返回 ARGB32 格式的 SHM 映射地址, 可直接读写.
 * 像素布局: pixel[y * width + x] = 0xAARRGGBB
 */
uint32_t *ws_get_buffer(ws_window_t *win);

/**
 * 获取窗口客户区尺寸
 */
void ws_get_size(ws_window_t *win, uint32_t *w, uint32_t *h);

/**
 * 获取窗口 ID
 */
uint32_t ws_get_id(ws_window_t *win);

/**
 * 提交脏区域 (通知服务器合成)
 *
 * @param x, y, w, h 脏矩形 (相对于客户区)
 * @return 0 成功, -1 失败
 */
int ws_submit(ws_window_t *win, uint32_t x, uint32_t y,
              uint32_t w, uint32_t h);

/**
 * 提交整个窗口
 */
int ws_submit_all(ws_window_t *win);

/**
 * 等待事件 (阻塞)
 *
 * @param win 窗口
 * @param ev  输出事件
 * @return 0 成功, -1 失败
 */
int ws_wait_event(ws_window_t *win, struct ws_event *ev);

/**
 * 查询屏幕信息
 */
int ws_get_screen_info(uint32_t *w, uint32_t *h);

/*
 * 本地绘图辅助 (直接写 SHM, 无 IPC)
 */

/**
 * 填充矩形 (ARGB32)
 */
void ws_fill_rect(ws_window_t *win, uint32_t x, uint32_t y,
                  uint32_t w, uint32_t h, uint32_t argb);

/**
 * 绘制单个字符
 */
void ws_draw_char(ws_window_t *win, uint32_t x, uint32_t y,
                  char c, uint32_t fg, uint32_t bg);

/**
 * 绘制文字
 */
void ws_draw_text(ws_window_t *win, uint32_t x, uint32_t y,
                  const char *text, uint32_t fg, uint32_t bg);

#endif /* WS_WS_H */
