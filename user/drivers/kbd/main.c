/**
 * @file main.c
 * @brief kbd UDM 驱动
 *
 * 键盘驱动兼输入管理器:
 * 1. 从 PS/2 读取扫描码并翻译
 * 2. 接收来自其他驱动(如 seriald)的输入
 * 3. 维护全局输入队列
 * 4. 通过 IPC 向用户程序提供输入
 */

#include "scancode.h"

#include <xnix/protocol/input.h>
#include <xnix/protocol/kbd.h>
#include <d/server.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/env.h>
#include <xnix/ipc/console.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define INPUT_BUF_SIZE 256

/* 环形输入缓冲区 */
static char              input_buf[INPUT_BUF_SIZE];
static volatile uint16_t input_head = 0;
static volatile uint16_t input_tail = 0;
static pthread_mutex_t   input_lock;

/* 前台进程 PID (用于 Ctrl+C) */
static pid_t foreground_pid = 0;

/* 挂起的 GETC 请求 */
static uint32_t        pending_getc_tid = 0; /* 0 表示无挂起请求 */
static pthread_mutex_t pending_lock;

/* 输入事件缓冲区 (GUI 路径) */
#define EVENT_BUF_SIZE 128
static struct input_event event_buf[EVENT_BUF_SIZE];
static volatile uint16_t event_head = 0;
static volatile uint16_t event_tail = 0;
static pthread_mutex_t   event_lock;

/* 挂起的事件读请求 */
static uint32_t        pending_event_tid = 0;
static pthread_mutex_t pending_event_lock;

/**
 * 写入字符到输入队列
 */
static void input_write_char(char c) {
    /* Ctrl+C (ETX, 0x03) - 发送 SIGINT 给前台进程 */
    if (c == 3 && foreground_pid > 1) {
        sys_kill(foreground_pid, SIGINT);
        return;
    }

    pthread_mutex_lock(&input_lock);

    uint16_t next = (input_head + 1) % INPUT_BUF_SIZE;
    if (next != input_tail) {
        input_buf[input_head] = c;
        input_head            = next;
    }

    pthread_mutex_unlock(&input_lock);
}

/**
 * 从输入队列读取字符 (非阻塞)
 * @return 字符值 (0-255), 或 -1 表示无数据
 */
static int input_read_char_nonblock(void) {
    pthread_mutex_lock(&input_lock);

    if (input_head != input_tail) {
        char c     = input_buf[input_tail];
        input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
        pthread_mutex_unlock(&input_lock);
        return (unsigned char)c;
    }

    pthread_mutex_unlock(&input_lock);
    return -1;
}

/**
 * 尝试满足挂起的 GETC 请求
 */
static void try_fulfill_pending_getc(void) {
    pthread_mutex_lock(&pending_lock);

    if (pending_getc_tid == 0) {
        pthread_mutex_unlock(&pending_lock);
        return;
    }

    int c = input_read_char_nonblock();
    if (c < 0) {
        pthread_mutex_unlock(&pending_lock);
        return;
    }

    /* 有数据且有挂起请求,延迟回复 */
    struct ipc_message reply = {0};
    reply.regs.data[0]       = (uint32_t)c;

    sys_ipc_reply_to(pending_getc_tid, &reply);
    pending_getc_tid = 0;

    pthread_mutex_unlock(&pending_lock);
}

/**
 * 写入事件到事件队列
 */
static void event_write(const struct input_event *ev) {
    pthread_mutex_lock(&event_lock);

    uint16_t next = (event_head + 1) % EVENT_BUF_SIZE;
    if (next != event_tail) {
        event_buf[event_head] = *ev;
        event_head            = next;
    }

    pthread_mutex_unlock(&event_lock);
}

/**
 * 从事件队列读取事件 (非阻塞)
 */
static int event_read(struct input_event *out) {
    pthread_mutex_lock(&event_lock);

    if (event_head == event_tail) {
        pthread_mutex_unlock(&event_lock);
        return -1;
    }

    *out       = event_buf[event_tail];
    event_tail = (event_tail + 1) % EVENT_BUF_SIZE;

    pthread_mutex_unlock(&event_lock);
    return 0;
}

/**
 * 尝试满足挂起的事件读请求
 */
static void try_fulfill_pending_event(void) {
    pthread_mutex_lock(&pending_event_lock);

    if (pending_event_tid == 0) {
        pthread_mutex_unlock(&pending_event_lock);
        return;
    }

    struct input_event ev;
    if (event_read(&ev) < 0) {
        pthread_mutex_unlock(&pending_event_lock);
        return;
    }

    struct ipc_message reply = {0};
    reply.regs.data[0] = 0;
    reply.regs.data[1] = INPUT_PACK_REG1(&ev);
    reply.regs.data[2] = INPUT_PACK_REG2(&ev);
    reply.regs.data[3] = INPUT_PACK_REG3(&ev);

    sys_ipc_reply_to(pending_event_tid, &reply);
    pending_event_tid = 0;

    pthread_mutex_unlock(&pending_event_lock);
}

/**
 * IPC 消息处理
 */
static int kbd_handler(struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case CONSOLE_OP_PUTC:
        /* 接收来自其他驱动的输入 */
        input_write_char((char)(msg->regs.data[1] & 0xFF));
        msg->regs.data[0] = 0;

        /* 检查是否有挂起的 GETC 请求 */
        try_fulfill_pending_getc();
        break;

    case CONSOLE_OP_GETC: {
        /* 尝试立即读取字符 */
        int c = input_read_char_nonblock();
        if (c >= 0) {
            /* 有数据,立即回复 */
            msg->regs.data[0] = (uint32_t)c;
        } else {
            /* 无数据,延迟回复 */
            pthread_mutex_lock(&pending_lock);
            if (msg->sender_tid == 0) {
                /* sender_tid 无效 */
                msg->regs.data[0] = (uint32_t)-1;
                pthread_mutex_unlock(&pending_lock);
            } else if (pending_getc_tid != 0 && pending_getc_tid != msg->sender_tid) {
                /* 已有不同的挂起请求 */
                msg->regs.data[0] = (uint32_t)-1;
                pthread_mutex_unlock(&pending_lock);
            } else {
                /* 保存请求供延迟回复 */
                pending_getc_tid = msg->sender_tid;
                pthread_mutex_unlock(&pending_lock);
                return 1; /* 不立即回复 */
            }
        }
        break;
    }

    case CONSOLE_OP_POLL: {
        /* 非阻塞检查 */
        pthread_mutex_lock(&input_lock);
        int has_input = (input_head != input_tail) ? 1 : 0;
        pthread_mutex_unlock(&input_lock);
        msg->regs.data[0] = has_input;
        break;
    }

    case CONSOLE_OP_SET_FOREGROUND:
        /* 设置前台进程 PID(用于 Ctrl+C) */
        foreground_pid    = (pid_t)(msg->regs.data[1]);
        msg->regs.data[0] = 0;
        break;

    case INPUT_OP_READ_EVENT: {
        struct input_event ev;
        if (event_read(&ev) == 0) {
            msg->regs.data[0] = 0;
            msg->regs.data[1] = INPUT_PACK_REG1(&ev);
            msg->regs.data[2] = INPUT_PACK_REG2(&ev);
            msg->regs.data[3] = INPUT_PACK_REG3(&ev);
        } else {
            pthread_mutex_lock(&pending_event_lock);
            if (msg->sender_tid == 0) {
                msg->regs.data[0] = (uint32_t)-1;
                pthread_mutex_unlock(&pending_event_lock);
            } else if (pending_event_tid != 0 &&
                       pending_event_tid != msg->sender_tid) {
                msg->regs.data[0] = (uint32_t)-1;
                pthread_mutex_unlock(&pending_event_lock);
            } else {
                pending_event_tid = msg->sender_tid;
                pthread_mutex_unlock(&pending_event_lock);
                return 1;
            }
        }
        break;
    }

    case INPUT_OP_POLL: {
        pthread_mutex_lock(&event_lock);
        int has_event = (event_head != event_tail) ? 1 : 0;
        pthread_mutex_unlock(&event_lock);
        msg->regs.data[0] = has_event;
        break;
    }

    default:
        msg->regs.data[0] = -1;
        break;
    }

    return 0;
}

/**
 * 键盘 IRQ 处理线程
 */
static void *keyboard_thread(void *arg) {
    (void)arg;

    /* 绑定 IRQ1 */
    int ret = sys_irq_bind(IRQ_KEYBOARD, -1, 0);
    if (ret < 0) {
        return NULL;
    }

    while (1) {
        uint8_t scancode;

        /* 阻塞读取 IRQ 数据 */
        ret = sys_irq_read(IRQ_KEYBOARD, &scancode, 1, 0);
        if (ret <= 0) {
            continue;
        }

        /* 生成输入事件 (GUI 路径) */
        struct input_event ev;
        if (scancode_to_event(scancode, &ev) == 0) {
            event_write(&ev);
            try_fulfill_pending_event();
        }

        /* 翻译扫描码 (TTY 路径) */
        int c = scancode_to_char(scancode);
        if (c >= 0) {
            input_write_char((char)c);
            try_fulfill_pending_getc();
        } else if (c <= KEY_UP && c >= KEY_RIGHT) {
            /* 方向键:发送 ANSI 转义序列 ESC [ A/B/C/D */
            static const char arrow_codes[] = {'A', 'B', 'D', 'C'};
            int               idx           = -(c + 2);
            input_write_char('\033');
            input_write_char('[');
            input_write_char(arrow_codes[idx]);
            try_fulfill_pending_getc();
        }
    }

    return NULL;
}

int main(void) {
    pthread_mutex_init(&input_lock, NULL);
    pthread_mutex_init(&pending_lock, NULL);
    pthread_mutex_init(&event_lock, NULL);
    pthread_mutex_init(&pending_event_lock, NULL);

    env_set_name("kbd");
    handle_t kbd_ep = env_require("kbd_ep");
    if (kbd_ep == HANDLE_INVALID) {
        return 1;
    }

    /* 启动键盘处理线程 */
    pthread_t kbd_tid;
    if (pthread_create(&kbd_tid, NULL, keyboard_thread, NULL) != 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[kbd]", " failed to create keyboard thread\n");
        return 1;
    }
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[kbd]", " started\n");

    /* 启动 IPC 服务器 */
    struct udm_server srv = {
        .endpoint = kbd_ep,
        .handler  = kbd_handler,
        .name     = "kbd",
    };

    udm_server_init(&srv);
    svc_notify_ready("kbd");
    udm_server_run(&srv);

    return 0;
}
