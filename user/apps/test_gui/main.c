/**
 * @file main.c
 * @brief GUI 演示应用
 *
 * 创建两个窗口:
 *   窗口 1: 彩色矩形 + 文字
 *   窗口 2: 事件日志
 */

#include <ws/ws.h>

#include <stdio.h>
#include <string.h>
#include <xnix/abi/input.h>
#include <xnix/env.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* 颜色常量 (ARGB32) */
#define COLOR_WHITE   0xFFFFFFFF
#define COLOR_BLACK   0xFF000000
#define COLOR_RED     0xFFFF4444
#define COLOR_GREEN   0xFF44CC44
#define COLOR_BLUE    0xFF4488FF
#define COLOR_YELLOW  0xFFFFCC00
#define COLOR_CYAN    0xFF44CCCC
#define COLOR_MAGENTA 0xFFCC44CC
#define COLOR_GRAY    0xFFCCCCCC
#define COLOR_DARK    0xFF222222

#define LOG_LINES     8
#define LOG_LINE_LEN  32

/**
 * 绘制窗口 1: 彩色矩形演示
 */
static void draw_window1(ws_window_t *win) {
    uint32_t w, h;
    ws_get_size(win, &w, &h);

    /* 背景 */
    ws_fill_rect(win, 0, 0, w, h, COLOR_DARK);

    /* 彩色矩形 */
    ws_fill_rect(win, 10, 10, 80, 60, COLOR_RED);
    ws_fill_rect(win, 100, 10, 80, 60, COLOR_GREEN);
    ws_fill_rect(win, 190, 10, 80, 60, COLOR_BLUE);
    ws_fill_rect(win, 10, 80, 80, 60, COLOR_YELLOW);
    ws_fill_rect(win, 100, 80, 80, 60, COLOR_CYAN);
    ws_fill_rect(win, 190, 80, 80, 60, COLOR_MAGENTA);

    /* 文字 */
    ws_draw_text(win, 10, 150, "Hello Xnix GUI!", COLOR_WHITE, COLOR_DARK);
    ws_draw_text(win, 10, 170, "Drag title bar to move", COLOR_GRAY, COLOR_DARK);
}

/**
 * 事件日志状态
 */
struct event_log {
    char lines[LOG_LINES][LOG_LINE_LEN];
    int  count;
    int  next;
};

static void log_add(struct event_log *log, const char *line) {
    size_t len = strlen(line);
    if (len >= LOG_LINE_LEN)
        len = LOG_LINE_LEN - 1;
    memcpy(log->lines[log->next], line, len);
    log->lines[log->next][len] = '\0';
    log->next = (log->next + 1) % LOG_LINES;
    if (log->count < LOG_LINES)
        log->count++;
}

/**
 * 绘制窗口 2: 事件日志
 */
static void draw_window2(ws_window_t *win, struct event_log *log) {
    uint32_t w, h;
    ws_get_size(win, &w, &h);

    /* 背景 */
    ws_fill_rect(win, 0, 0, w, h, COLOR_BLACK);

    /* 标题 */
    ws_draw_text(win, 4, 4, "Event Log:", COLOR_YELLOW, COLOR_BLACK);

    /* 日志行 */
    int start = (log->next - log->count + LOG_LINES) % LOG_LINES;
    for (int i = 0; i < log->count; i++) {
        int idx = (start + i) % LOG_LINES;
        ws_draw_text(win, 4, 24 + i * 16, log->lines[idx],
                     COLOR_GREEN, COLOR_BLACK);
    }
}

static const char *event_type_str(uint8_t type) {
    switch (type) {
    case INPUT_EVENT_KEY_PRESS:    return "KEY_DN";
    case INPUT_EVENT_KEY_RELEASE:  return "KEY_UP";
    case INPUT_EVENT_MOUSE_MOVE:   return "M_MOVE";
    case INPUT_EVENT_MOUSE_BUTTON: return "M_BTN";
    default:                       return "?";
    }
}

int main(void) {
    env_set_name("test_gui");

    if (ws_connect() < 0) {
        ulog_errf("[test_gui] Failed to connect to wsd\n");
        return 1;
    }

    /* 先创建日志窗口，再创建图形窗口。
     * wsd 会默认把最后创建的窗口设为焦点，
     * 这样图形窗口 win1 在启动后就是默认焦点窗口。 */
    ws_window_t *win2 = ws_create_window(250, 160, "Event Log");
    if (!win2) {
        ulog_errf("[test_gui] Failed to create log window\n");
        return 1;
    }

    /* 创建窗口 1 */
    ws_window_t *win1 = ws_create_window(280, 190, "Graphics Demo");
    if (!win1) {
        ulog_errf("[test_gui] Failed to create graphics window\n");
        return 1;
    }

    /* 初始绘制 */
    draw_window1(win1);
    ws_submit_all(win1);

    struct event_log log;
    memset(&log, 0, sizeof(log));
    log_add(&log, "Waiting for events...");
    draw_window2(win2, &log);
    ws_submit_all(win2);

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[test_gui]",
              " Windows created, entering event loop\n");

    /* 事件循环: 只监听 win1, win2 的 CLOSE 由 wsd 直接销毁 */
    while (1) {
        struct ws_event ev;

        if (ws_wait_event(win1, &ev) == 0) {
            if (ev.type == WS_EV_CLOSE) {
                break;
            }

            if (ev.type == WS_EV_INPUT) {
                char buf[LOG_LINE_LEN];
                snprintf(buf, sizeof(buf), "%s c=%u v=%d",
                         event_type_str(ev.input.type),
                         ev.input.code, (int)ev.input.value);
                log_add(&log, buf);
                draw_window2(win2, &log);
                ws_submit_all(win2);
            }

            if (ev.type == WS_EV_FOCUS) {
                log_add(&log, ev.focused ? "W1 FOCUS+" : "W1 FOCUS-");
                draw_window2(win2, &log);
                ws_submit_all(win2);
            }
        }
    }

    ws_destroy_window(win1);
    ws_destroy_window(win2);
    return 0;
}
