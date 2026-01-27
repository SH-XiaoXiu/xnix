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

/* ANSI 颜色码,用于异步输出 */
static const char *ansi_colors[] = {
    "\033[30m", "\033[34m", "\033[32m", "\033[36m", "\033[31m", "\033[35m", "\033[33m", "\033[37m",
    "\033[90m", "\033[94m", "\033[92m", "\033[96m", "\033[91m", "\033[95m", "\033[93m", "\033[97m",
};

static struct console *consoles[MAX_CONSOLES];
static int             console_count = 0;

/* Ring buffer for async drivers */
static char           g_ringbuf_data[CONSOLE_RINGBUF_SIZE];
static struct ringbuf g_ringbuf;
static bool           g_async_enabled = false;

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

void console_async_enable(void) {
    g_async_enabled = true;
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

void console_putc(char c) {
    /* 同步驱动立即调用 */
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->putc) {
            consoles[i]->putc(c);
        }
    }

    /* 异步驱动: 写入 buffer */
    if (g_async_enabled) {
        uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
        while (ringbuf_full(&g_ringbuf)) {
            /* Buffer 满: 短暂让出,等待消费者 */
            spin_unlock_irqrestore(&g_ringbuf.lock, flags);
            thread_yield();
            flags = spin_lock_irqsave(&g_ringbuf.lock);
        }
        ringbuf_put(&g_ringbuf, c);
        spin_unlock_irqrestore(&g_ringbuf.lock, flags);
    }
}

void console_puts(const char *s) {
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->puts) {
            consoles[i]->puts(s);
        }
    }

    if (g_async_enabled) {
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
    }
}

void console_set_color(kcolor_t color) {
    /* 同步驱动立即调用 */
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->set_color) {
            consoles[i]->set_color(color);
        }
    }

    /* 异步驱动: 写入 ANSI 序列到 buffer */
    if (g_async_enabled && color >= 0 && color <= 15) {
        uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
        ringbuf_puts_locked(ansi_colors[color]);
        spin_unlock_irqrestore(&g_ringbuf.lock, flags);
    }
}

void console_reset_color(void) {
    /* 同步驱动立即调用 */
    for (int i = 0; i < console_count; i++) {
        if ((consoles[i]->flags & CONSOLE_ASYNC) == 0 && consoles[i]->reset_color) {
            consoles[i]->reset_color();
        }
    }

    /* 异步驱动: 写入 ANSI 重置序列到 buffer */
    if (g_async_enabled) {
        uint32_t flags = spin_lock_irqsave(&g_ringbuf.lock);
        ringbuf_puts_locked("\033[0m");
        spin_unlock_irqrestore(&g_ringbuf.lock, flags);
    }
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
