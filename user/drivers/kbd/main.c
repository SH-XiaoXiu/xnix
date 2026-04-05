/**
 * @file main.c
 * @brief kbd — 键盘 inputdev 驱动
 *
 * 通过 libdriver inputdev 框架注册为输入设备.
 * IRQ 线程读取 PS/2 扫描码, 翻译为 input_event 放入队列,
 * inputdev 框架的 read_event() 从队列取事件返回给消费者.
 */

#include "scancode.h"

#include <xnix/inputdev.h>
#include <xnix/drvframework.h>
#include <xnix/protocol/inputdev.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define IRQ_KEYBOARD 1

/* ============== 事件队列 ============== */

#define EVENT_BUF_SIZE 128

static struct input_event event_buf[EVENT_BUF_SIZE];
static volatile uint16_t  event_head = 0;
static volatile uint16_t  event_tail = 0;
static pthread_mutex_t    event_lock;
static handle_t           event_notif = HANDLE_INVALID;

static void event_put(const struct input_event *ev) {
    pthread_mutex_lock(&event_lock);
    uint16_t next = (event_head + 1) % EVENT_BUF_SIZE;
    if (next != event_tail) {
        event_buf[event_head] = *ev;
        event_head = next;
    }
    pthread_mutex_unlock(&event_lock);
}

static int event_get(struct input_event *out) {
    pthread_mutex_lock(&event_lock);
    if (event_head == event_tail) {
        pthread_mutex_unlock(&event_lock);
        return -1;
    }
    *out = event_buf[event_tail];
    event_tail = (event_tail + 1) % EVENT_BUF_SIZE;
    pthread_mutex_unlock(&event_lock);
    return 0;
}

static int event_count(void) {
    pthread_mutex_lock(&event_lock);
    int n;
    if (event_head >= event_tail)
        n = event_head - event_tail;
    else
        n = EVENT_BUF_SIZE - event_tail + event_head;
    pthread_mutex_unlock(&event_lock);
    return n;
}

/* ============== inputdev ops ============== */

static int kbd_read_event(struct input_device *dev, struct input_event *ev) {
    (void)dev;

    while (1) {
        if (event_get(ev) == 0)
            return 0;

        /* 等待 IRQ 线程唤醒 */
        sys_notification_wait(event_notif);
    }
}

static int kbd_poll(struct input_device *dev) {
    (void)dev;
    return event_count();
}

static struct input_ops kbd_ops = {
    .read_event = kbd_read_event,
    .poll       = kbd_poll,
};

/* ============== IRQ 线程 ============== */

static void *keyboard_thread(void *arg) {
    (void)arg;

    int ret = sys_irq_bind(IRQ_KEYBOARD, -1, 0);
    if (ret < 0) {
        ulog_errf("[kbd] IRQ bind failed: %d\n", ret);
        return NULL;
    }

    while (1) {
        uint8_t scancode;
        ret = sys_irq_read(IRQ_KEYBOARD, &scancode, 1, 0);
        if (ret <= 0)
            continue;

        struct input_event ev;
        if (scancode_to_event(scancode, &ev) == 0) {
            event_put(&ev);
            sys_notification_signal(event_notif, 1);
        }
    }

    return NULL;
}

/* ============== 入口 ============== */

int main(void) {
    pthread_mutex_init(&event_lock, NULL);

    env_set_name("kbd");

    event_notif = sys_notification_create();

    /* 注册 inputdev */
    struct input_device dev = {
        .name     = "ps2_kbd",
        .instance = 0,
        .ops      = &kbd_ops,
        .caps     = INPUTDEV_CAP_KEY,
        .type     = INPUTDEV_TYPE_KEYBOARD,
        .endpoint = env_get_handle("kbd_ep"),
    };

    if (inputdev_register(&dev) < 0) {
        ulog_errf("[kbd] register failed\n");
        return 1;
    }

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[kbd]", " started\n");

    /* 启动 IRQ 线程 */
    pthread_t tid;
    pthread_create(&tid, NULL, keyboard_thread, NULL);

    svc_notify_ready("kbd");

    driver_run();

    return 0;
}
