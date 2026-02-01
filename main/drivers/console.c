/**
 * @file console.c
 * @brief 控制台驱动框架
 *
 * 支持同步/异步两种模式:
 * - 同步驱动(VGA): putc 时立即调用
 * - 异步驱动(Serial): 写入 ring buffer,由消费者线程处理
 */

#include <xnix/console.h>
#include <xnix/ringbuf.h>
#include <xnix/string.h>
#include <xnix/thread.h>

#define MAX_CONSOLES         4
#define CONSOLE_RINGBUF_SIZE 4096

/* ANSI 颜色码 (kcolor_t 到 ANSI 序列的映射) */
static const char *ansi_colors[] = {
    "\x1b[30m", /* KCOLOR_BLACK */
    "\x1b[34m", /* KCOLOR_BLUE */
    "\x1b[32m", /* KCOLOR_GREEN */
    "\x1b[36m", /* KCOLOR_CYAN */
    "\x1b[31m", /* KCOLOR_RED */
    "\x1b[35m", /* KCOLOR_MAGENTA */
    "\x1b[33m", /* KCOLOR_BROWN/YELLOW */
    "\x1b[37m", /* KCOLOR_LIGHT_GREY */
    "\x1b[90m", /* KCOLOR_DARK_GREY */
    "\x1b[94m", /* KCOLOR_LIGHT_BLUE */
    "\x1b[92m", /* KCOLOR_LIGHT_GREEN */
    "\x1b[96m", /* KCOLOR_LIGHT_CYAN */
    "\x1b[91m", /* KCOLOR_LIGHT_RED */
    "\x1b[95m", /* KCOLOR_PINK */
    "\x1b[93m", /* KCOLOR_YELLOW */
    "\x1b[97m", /* KCOLOR_WHITE */
};

static struct console *consoles[MAX_CONSOLES];
static int             console_count = 0;

/* Ring buffer for async drivers */
static char           g_ringbuf_data[CONSOLE_RINGBUF_SIZE];
static struct ringbuf g_ringbuf;
static bool           g_async_enabled  = false;
static bool           g_emergency_mode = false;

/* 紧急直接输出函数 (由 serial 驱动注册) */
static void (*g_emergency_putc)(char c) = NULL;

int console_register(struct console *c) {
    if (console_count >= MAX_CONSOLES || !c) {
        return -1;
    }
    consoles[console_count++] = c;
    return 0;
}

int console_replace(const char *name, struct console *c) {
    if (!name || !c) {
        return -1;
    }

    for (int i = 0; i < console_count; i++) {
        if (consoles[i] && consoles[i]->name && !strcmp(consoles[i]->name, name)) {
            consoles[i] = c;
            return 0;
        }
    }

    return -1;
}

void console_init(void) {
    ringbuf_init(&g_ringbuf, g_ringbuf_data, CONSOLE_RINGBUF_SIZE);

    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->init) {
            consoles[i]->init();
        }
    }
}

void console_start_consumers(void) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->start_consumer) {
            consoles[i]->start_consumer();
        }
    }
}

void console_async_enable(void) {
    g_async_enabled = true;
}

void console_emergency_mode(void) {
    g_emergency_mode = true;
    g_async_enabled  = false;
}

void console_register_emergency_putc(void (*putc)(char c)) {
    g_emergency_putc = putc;
}

int console_ringbuf_get(char *c) {
    uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
    int      ret   = ringbuf_get(&g_ringbuf, c);
    spin_unlock_irqrestore(&g_ringbuf.lock, flags);
    return ret;
}

void console_flush(void) {
    while (!ringbuf_empty(&g_ringbuf)) {
        thread_yield();
    }
}

/* 将字符串写入 ring buffer(内部用,已持锁) */
static void ringbuf_puts_locked(const char *s) {
    while (*s) {
        while (ringbuf_full(&g_ringbuf)) {
            spin_unlock(&g_ringbuf.lock);
            thread_yield();
            spin_lock(&g_ringbuf.lock);
        }
        ringbuf_put(&g_ringbuf, *s++);
    }
}

extern struct thread *sched_current(void);

void console_putc(char c) {
    /* 同步驱动立即调用 */
    bool has_sync_output = false;
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->putc) {
            consoles[i]->putc(c);
            has_sync_output = true;
        }
    }

    /* 紧急模式: 直接输出到串口 */
    if (g_emergency_mode) {
        if (g_emergency_putc) {
            if (c == '\n') {
                g_emergency_putc('\r');
            }
            g_emergency_putc(c);
        }
        return;
    }

    /* 异步驱动: 写入 buffer */
    if (!g_async_enabled) {
        return;
    }

    if (sched_current()) {
        uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
        while (ringbuf_full(&g_ringbuf)) {
            spin_unlock_irqrestore(&g_ringbuf.lock, flags);
            thread_yield();
            flags = spin_lock_irqsave(&g_ringbuf.lock);
        }
        ringbuf_put(&g_ringbuf, c);
        spin_unlock_irqrestore(&g_ringbuf.lock, flags);
        return;
    }

    uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
    if (!ringbuf_full(&g_ringbuf)) {
        ringbuf_put(&g_ringbuf, c);
        spin_unlock_irqrestore(&g_ringbuf.lock, flags);
        return;
    }
    spin_unlock_irqrestore(&g_ringbuf.lock, flags);

    if (!has_sync_output && g_emergency_putc) {
        if (c == '\n') {
            g_emergency_putc('\r');
        }
        g_emergency_putc(c);
    }
}

void console_puts(const char *s) {
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->puts) {
            consoles[i]->puts(s);
        }
    }

    /* 紧急模式: 直接输出到串口 */
    if (g_emergency_mode) {
        if (g_emergency_putc) {
            while (*s) {
                if (*s == '\n') {
                    g_emergency_putc('\r');
                }
                g_emergency_putc(*s++);
            }
        }
        return;
    }

    if (!g_async_enabled) {
        return;
    }

    if (sched_current()) {
        while (*s) {
            uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
            while (ringbuf_full(&g_ringbuf)) {
                spin_unlock_irqrestore(&g_ringbuf.lock, flags);
                thread_yield();
                flags = spin_lock_irqsave(&g_ringbuf.lock);
            }
            ringbuf_put(&g_ringbuf, *s++);
            spin_unlock_irqrestore(&g_ringbuf.lock, flags);
        }
        return;
    }

    uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
    while (*s && !ringbuf_full(&g_ringbuf)) {
        ringbuf_put(&g_ringbuf, *s++);
    }
    spin_unlock_irqrestore(&g_ringbuf.lock, flags);
}

void console_set_color(kcolor_t color) {
    /* 统一使用 ANSI 序列输出颜色 */
    if (color >= 0 && color <= 15) {
        console_puts(ansi_colors[color]);
    }
}

void console_reset_color(void) {
    /* 统一使用 ANSI 序列重置颜色 */
    console_puts("\x1b[0m");
}

void console_clear(void) {
    /* 同步驱动立即调用 */
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->clear) {
            consoles[i]->clear();
        }
    }

    /* 异步驱动: 写入 ANSI 清屏序列到 buffer */
    if (g_async_enabled) {
        uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
        ringbuf_puts_locked("\033[2J\033[H");
        spin_unlock_irqrestore(&g_ringbuf.lock, flags);
    }
}
