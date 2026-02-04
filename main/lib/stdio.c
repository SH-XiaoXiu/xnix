/**
 * @file stdio.c
 * @brief 内核标准输出实现
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <xnix/console.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>
#include <xnix/types.h>

/* 输出锁,保证多核输出不交错(导出给 sys_write 使用) */
spinlock_t kprintf_lock = SPINLOCK_INIT;

typedef void (*print_fn)(void *ctx, char c);

struct snprintf_ctx {
    char  *buf;
    size_t size;
    size_t pos;
};

static void kputc_wrapper(void *ctx, char c) {
    (void)ctx;
    if (c == '\n') {
        console_putc('\r');
    }
    console_putc(c);
}

static void snprintf_wrapper(void *ctx, char c) {
    struct snprintf_ctx *s = ctx;
    if (s->pos < s->size - 1) {
        s->buf[s->pos++] = c;
    } else if (s->size > 0) {
        s->pos++; /* 记录总长度,但不写入 */
    }
}

static void print_padding(print_fn emit, void *ctx, int width, int len, int pad_zero) {
    char padc = pad_zero ? '0' : ' ';
    while (width-- > len) {
        emit(ctx, padc);
    }
}

static int utoa_buf(uint32_t num, int base, char *buf) {
    static const char digits[] = "0123456789abcdef";
    int               i        = 0;

    if (num == 0) {
        buf[i++] = '0';
        return i;
    }

    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }

    for (int j = 0; j < i / 2; j++) {
        char t         = buf[j];
        buf[j]         = buf[i - j - 1];
        buf[i - j - 1] = t;
    }
    return i;
}

static int itoa_buf(int32_t num, char *buf) {
    if (num < 0) {
        buf[0] = '-';
        return 1 + utoa_buf((uint32_t)(-(int64_t)num), 10, buf + 1);
    }
    return utoa_buf((uint32_t)num, 10, buf);
}

static inline void print_hex_padded(print_fn emit, void *ctx, uint32_t num, int width) {
    static const char digits[] = "0123456789abcdef";
    for (int i = width - 1; i >= 0; i--) {
        emit(ctx, digits[(num >> (i * 4)) & 0xf]);
    }
}

static void do_printf(print_fn emit, void *ctx, const char *fmt, __builtin_va_list args) {
    if (!fmt) {
        return;
    }

    while (*fmt) {
        if (*fmt != '%') {
            emit(ctx, *fmt++);
            continue;
        }

        fmt++;
        /* 处理宽度修饰符*/
        int width    = 0;
        int pad_zero = 0;
        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s   = __builtin_va_arg(args, const char *) ?: "(null)";
            int         len = 0;
            while (s[len]) {
                len++;
            }
            print_padding(emit, ctx, width, len, 0);
            while (*s) {
                emit(ctx, *s++);
            }
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(args, int);
            print_padding(emit, ctx, width, 1, 0);
            emit(ctx, c);
            break;
        }
        case 'd':
        case 'i': {
            char    buf[32];
            int32_t v   = __builtin_va_arg(args, int32_t);
            int     len = itoa_buf(v, buf);

            if (pad_zero && buf[0] == '-') {
                emit(ctx, '-');
                print_padding(emit, ctx, width, len, 1);
                for (int i = 1; i < len; i++) {
                    emit(ctx, buf[i]);
                }
            } else {
                print_padding(emit, ctx, width, len, pad_zero);
                for (int i = 0; i < len; i++) {
                    emit(ctx, buf[i]);
                }
            }
            break;
        }
        case 'u': {
            char buf[32];
            int  len = utoa_buf(__builtin_va_arg(args, uint32_t), 10, buf);
            print_padding(emit, ctx, width, len, pad_zero);
            for (int i = 0; i < len; i++) {
                emit(ctx, buf[i]);
            }
            break;
        }
        case 'x': {
            uint32_t v = __builtin_va_arg(args, uint32_t);
            if (pad_zero && width > 0) {
                print_hex_padded(emit, ctx, v, width);
            } else {
                char buf[32];
                int  len = utoa_buf(v, 16, buf);
                print_padding(emit, ctx, width, len, pad_zero);
                for (int i = 0; i < len; i++) {
                    emit(ctx, buf[i]);
                }
            }
            break;
        }
        case 'p':
            if (ctx == NULL) {
                kputs("0x");
            } else {
                emit(ctx, '0');
                emit(ctx, 'x');
            }

            print_hex_padded(emit, ctx, (uint32_t)(uintptr_t)__builtin_va_arg(args, void *), 8);
            break;
        case '%':
            emit(ctx, '%');
            break;
        /* 颜色格式符 - 仅对 kprintf 有效 */
        case 'K':
            if (!ctx) {
                console_set_color(KCOLOR_BLACK);
            }
            break;
        case 'R':
            if (!ctx) {
                console_set_color(KCOLOR_RED);
            }
            break;
        case 'G':
            if (!ctx) {
                console_set_color(KCOLOR_GREEN);
            }
            break;
        case 'Y':
            if (!ctx) {
                console_set_color(KCOLOR_YELLOW);
            }
            break;
        case 'B':
            if (!ctx) {
                console_set_color(KCOLOR_BLUE);
            }
            break;
        case 'M':
            if (!ctx) {
                console_set_color(KCOLOR_MAGENTA);
            }
            break;
        case 'C':
            if (!ctx) {
                console_set_color(KCOLOR_CYAN);
            }
            break;
        case 'W':
            if (!ctx) {
                console_set_color(KCOLOR_WHITE);
            }
            break;
        case 'N':
            if (!ctx) {
                console_reset_color();
            }
            break;
        default:
            emit(ctx, '%');
            emit(ctx, *fmt);
            break;
        }
        fmt++;
    }
}

void kputc(char c) {
    kputc_wrapper(NULL, c);
}

void kputs(const char *str) {
    if (!str) {
        return;
    }
    while (*str) {
        kputc(*str++);
    }
}

void vkprintf(const char *fmt, __builtin_va_list args) {
    do_printf(kputc_wrapper, NULL, fmt, args);
}

void kprintf(const char *fmt, ...) {
    uint32_t          flags = spin_lock_irqsave(&kprintf_lock);
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vkprintf(fmt, args);
    __builtin_va_end(args);
    spin_unlock_irqrestore(&kprintf_lock, flags);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    struct snprintf_ctx ctx = {.buf = buf, .size = size, .pos = 0};
    __builtin_va_list   args;
    __builtin_va_start(args, fmt);
    do_printf(snprintf_wrapper, &ctx, fmt, args);
    __builtin_va_end(args);

    if (size > 0) {
        if (ctx.pos < size) {
            buf[ctx.pos] = '\0';
        } else {
            buf[size - 1] = '\0';
        }
    }
    return (int)ctx.pos;
}

void klog(int level, const char *fmt, ...) {
    uint32_t flags = spin_lock_irqsave(&kprintf_lock);

    switch (level) {
    case LOG_ERR:
        console_set_color(KCOLOR_RED);
        kputs("[ERR] ");
        break;
    case LOG_WARN:
        console_set_color(KCOLOR_YELLOW);
        kputs("[WARN] ");
        break;
    case LOG_INFO:
        console_set_color(KCOLOR_WHITE);
        kputs("[INFO] ");
        break;
    case LOG_OK:
        console_set_color(KCOLOR_GREEN);
        kputs("[OK]   ");
        console_reset_color();
        break;
    case LOG_DBG:
        console_set_color(KCOLOR_DGRAY);
        kputs("[DBG]  ");
        break;
    default:
        break;
    }

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vkprintf(fmt, args);
    __builtin_va_end(args);

    /* 重置颜色并确保换行 */
    console_reset_color();
    kputc('\n');

    spin_unlock_irqrestore(&kprintf_lock, flags);
}
