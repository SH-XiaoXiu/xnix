/**
 * @file main.c
 * @brief ps2 - unified PS/2 keyboard + mouse driver
 */

#include "scancode.h"
#include "ps2_mouse.h"

#include <xnix/drvframework.h>
#include <xnix/inputdev.h>
#include <xnix/abi/irq.h>
#include <xnix/protocol/inputdev.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define IRQ_KEYBOARD 1
#define IRQ_MOUSE    12
#define EVENT_BUF_SIZE 128

struct input_queue {
    struct input_event buf[EVENT_BUF_SIZE];
    volatile uint16_t  head;
    volatile uint16_t  tail;
    pthread_mutex_t    lock;
    handle_t           notif;
};

static struct input_queue g_kbd_queue;
static struct input_queue g_mouse_queue;

static void drain_irq_bytes(uint8_t irq) {
    uint8_t byte;
    while (sys_irq_read(irq, &byte, 1, IRQ_READ_NONBLOCK) > 0) {
    }
}

static void input_queue_init(struct input_queue *q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->lock, NULL);
    q->notif = sys_event_create();
}

static void input_queue_put(struct input_queue *q, const struct input_event *ev) {
    pthread_mutex_lock(&q->lock);
    uint16_t next = (q->head + 1) % EVENT_BUF_SIZE;
    if (next != q->tail) {
        q->buf[q->head] = *ev;
        q->head         = next;
    }
    pthread_mutex_unlock(&q->lock);
}

static void mouse_queue_put(struct input_queue *q, const struct input_event *ev) {
    pthread_mutex_lock(&q->lock);

    if (ev->type == INPUT_EVENT_MOUSE_MOVE && q->head != q->tail) {
        uint16_t last = (q->head == 0) ? (EVENT_BUF_SIZE - 1) : (q->head - 1);
        struct input_event *prev = &q->buf[last];
        if (prev->type == INPUT_EVENT_MOUSE_MOVE) {
            int32_t dx = (int32_t)prev->value + (int32_t)ev->value;
            int32_t dy = (int32_t)prev->value2 + (int32_t)ev->value2;

            if (dx > 32767) dx = 32767;
            if (dx < -32768) dx = -32768;
            if (dy > 32767) dy = 32767;
            if (dy < -32768) dy = -32768;

            prev->value  = (int16_t)dx;
            prev->value2 = (int16_t)dy;
            pthread_mutex_unlock(&q->lock);
            return;
        }
    }

    uint16_t next = (q->head + 1) % EVENT_BUF_SIZE;
    if (next != q->tail) {
        q->buf[q->head] = *ev;
        q->head         = next;
    } else if (ev->type == INPUT_EVENT_MOUSE_MOVE && q->head != q->tail) {
        uint16_t last = (q->head == 0) ? (EVENT_BUF_SIZE - 1) : (q->head - 1);
        struct input_event *prev = &q->buf[last];
        if (prev->type == INPUT_EVENT_MOUSE_MOVE) {
            int32_t dx = (int32_t)prev->value + (int32_t)ev->value;
            int32_t dy = (int32_t)prev->value2 + (int32_t)ev->value2;

            if (dx > 32767) dx = 32767;
            if (dx < -32768) dx = -32768;
            if (dy > 32767) dy = 32767;
            if (dy < -32768) dy = -32768;

            prev->value  = (int16_t)dx;
            prev->value2 = (int16_t)dy;
        }
    }

    pthread_mutex_unlock(&q->lock);
}

static int input_queue_get(struct input_queue *q, struct input_event *ev) {
    pthread_mutex_lock(&q->lock);
    if (q->head == q->tail) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    *ev     = q->buf[q->tail];
    q->tail = (q->tail + 1) % EVENT_BUF_SIZE;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static int input_queue_count(struct input_queue *q) {
    pthread_mutex_lock(&q->lock);
    int count;
    if (q->head >= q->tail) {
        count = q->head - q->tail;
    } else {
        count = EVENT_BUF_SIZE - q->tail + q->head;
    }
    pthread_mutex_unlock(&q->lock);
    return count;
}

static int queue_read_event(struct input_queue *q, struct input_event *ev) {
    while (1) {
        if (input_queue_get(q, ev) == 0) {
            return 0;
        }
        sys_event_wait(q->notif);
    }
}

static int queue_poll(struct input_queue *q) {
    return input_queue_count(q);
}

static int ps2_kbd_read_event(struct input_device *dev, struct input_event *ev) {
    return queue_read_event((struct input_queue *)dev->priv, ev);
}

static int ps2_kbd_poll(struct input_device *dev) {
    return queue_poll((struct input_queue *)dev->priv);
}

static int ps2_mouse_read_event(struct input_device *dev, struct input_event *ev) {
    return queue_read_event((struct input_queue *)dev->priv, ev);
}

static int ps2_mouse_poll(struct input_device *dev) {
    return queue_poll((struct input_queue *)dev->priv);
}

static struct input_ops g_kbd_ops = {
    .read_event = ps2_kbd_read_event,
    .poll       = ps2_kbd_poll,
};

static struct input_ops g_mouse_ops = {
    .read_event = ps2_mouse_read_event,
    .poll       = ps2_mouse_poll,
};

static void *keyboard_thread(void *arg) {
    (void)arg;

    int ret = sys_irq_bind(IRQ_KEYBOARD, -1, 0);
    if (ret < 0) {
        ulog_errf("[ps2] keyboard IRQ bind failed: %d\n", ret);
        return NULL;
    }

    /* 丢弃驱动启动前积压在 IRQ ring 里的残留字节，避免启动后注入脏输入。 */
    drain_irq_bytes(IRQ_KEYBOARD);

    while (1) {
        uint8_t scancode;
        ret = sys_irq_read(IRQ_KEYBOARD, &scancode, 1, 0);
        if (ret <= 0) {
            continue;
        }

        struct input_event ev;
        if (scancode_to_event(scancode, &ev) < 0) {
            continue;
        }

        input_queue_put(&g_kbd_queue, &ev);
        sys_event_signal(g_kbd_queue.notif, 1);
    }

    return NULL;
}

static void emit_mouse_event(uint8_t type, uint16_t code, int16_t value, int16_t value2) {
    struct input_event ev = {
        .type      = type,
        .modifiers = 0,
        .code      = code,
        .value     = value,
        .value2    = value2,
        .timestamp = 0,
        ._reserved = 0,
    };
    mouse_queue_put(&g_mouse_queue, &ev);
    sys_event_signal(g_mouse_queue.notif, 1);
}

static void *mouse_thread(void *arg) {
    (void)arg;

    if (ps2_mouse_init() < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[ps2]",
                  " PS/2 mouse init failed\n");
        return NULL;
    }

    int ret = sys_irq_bind(IRQ_MOUSE, -1, 0);
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[ps2]",
                  " failed to bind IRQ %d\n", IRQ_MOUSE);
        return NULL;
    }

    uint8_t packet[3];
    int     byte_idx      = 0;
    uint8_t prev_buttons  = 0;

    while (1) {
        uint8_t byte;
        ret = sys_irq_read(IRQ_MOUSE, &byte, 1, 0);
        if (ret <= 0) {
            continue;
        }

        if (byte_idx == 0 && !(byte & 0x08)) {
            continue;
        }

        packet[byte_idx++] = byte;
        if (byte_idx != 3) {
            continue;
        }
        byte_idx = 0;

        int16_t dx, dy;
        uint8_t buttons;
        if (ps2_mouse_parse(packet, &dx, &dy, &buttons) < 0) {
            continue;
        }

        if (dx != 0 || dy != 0) {
            emit_mouse_event(INPUT_EVENT_MOUSE_MOVE, 0, dx, dy);
        }

        uint8_t changed = buttons ^ prev_buttons;
        for (int i = 0; i < 3; i++) {
            if (changed & (1u << i)) {
                emit_mouse_event(INPUT_EVENT_MOUSE_BUTTON, (uint16_t)i,
                                 (buttons & (1u << i)) ? 1 : 0, 0);
            }
        }
        prev_buttons = buttons;
    }

    return NULL;
}

int main(void) {
    input_queue_init(&g_kbd_queue);
    input_queue_init(&g_mouse_queue);

    struct input_device kbd = {
        .name     = "ps2_kbd",
        .instance = 0,
        .ops      = &g_kbd_ops,
        .caps     = INPUTDEV_CAP_KEY,
        .type     = INPUTDEV_TYPE_KEYBOARD,
        .priv     = &g_kbd_queue,
        .endpoint = env_get_handle("kbd_ep"),
    };

    struct input_device mouse = {
        .name     = "ps2_mouse",
        .instance = 0,
        .ops      = &g_mouse_ops,
        .caps     = INPUTDEV_CAP_MOUSE,
        .type     = INPUTDEV_TYPE_MOUSE,
        .priv     = &g_mouse_queue,
        .endpoint = env_get_handle("mouse_ep"),
    };

    if (kbd.endpoint == HANDLE_INVALID || mouse.endpoint == HANDLE_INVALID) {
        ulog_errf("[ps2] missing kbd_ep or mouse_ep\n");
        return 1;
    }

    if (inputdev_register(&kbd) < 0 || inputdev_register(&mouse) < 0) {
        ulog_errf("[ps2] register failed\n");
        return 1;
    }

    pthread_t kbd_tid;
    pthread_t mouse_tid;
    if (pthread_create(&kbd_tid, NULL, keyboard_thread, NULL) != 0) {
        ulog_errf("[ps2] failed to create keyboard thread\n");
        return 1;
    }
    if (pthread_create(&mouse_tid, NULL, mouse_thread, NULL) != 0) {
        ulog_errf("[ps2] failed to create mouse thread\n");
        return 1;
    }

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[ps2]",
              " keyboard+mouse started\n");

    svc_notify_ready("ps2");
    driver_run();
    return 0;
}
