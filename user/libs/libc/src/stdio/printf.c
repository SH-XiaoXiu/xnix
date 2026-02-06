/**
 * @file printf.c
 * @brief 格式化输出实现
 *
 * 所有输出通过 FILE stream 到 ttyd.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdio_internal.h>
#include <string.h>

/* 输出回调函数类型 */
typedef void (*emit_fn)(char c, void *ctx);

/* 缓冲区输出上下文 */
struct buf_ctx {
    char  *buf;
    size_t size;
    size_t pos;
};

static void buf_emit(char c, void *ctx) {
    struct buf_ctx *b = (struct buf_ctx *)ctx;
    if (b->buf && b->pos < b->size - 1) {
        b->buf[b->pos] = c;
    }
    b->pos++;
}

/* FILE 输出 */
static void file_emit(char c, void *ctx) {
    _file_putc((FILE *)ctx, c);
}

/* 数字格式化输出 */
static int emit_num(emit_fn emit, void *ctx, unsigned int num, int base,
                    int is_signed, int width, int pad_zero) {
    char         tmp[32];
    int          i        = 0;
    int          negative = 0;
    unsigned int n        = num;

    if (is_signed && (int)num < 0) {
        negative = 1;
        n        = -(int)num;
    }

    if (n == 0) {
        tmp[i++] = '0';
    } else {
        while (n) {
            int d    = n % base;
            tmp[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
            n /= base;
        }
    }

    if (negative) {
        tmp[i++] = '-';
    }

    int len = i;
    int pad = (width > len) ? (width - len) : 0;

    int written = 0;
    while (pad-- > 0) {
        emit(pad_zero ? '0' : ' ', ctx);
        written++;
    }
    while (i-- > 0) {
        emit(tmp[i], ctx);
        written++;
    }

    return written;
}

/* 核心格式化引擎 */
static int do_printf(emit_fn emit, void *ctx, const char *fmt, va_list ap) {
    int written = 0;

    while (*fmt) {
        if (*fmt != '%') {
            emit(*fmt, ctx);
            written++;
            fmt++;
            continue;
        }

        fmt++; /* skip '%' */

        /* 解析宽度和标志 */
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
        case 'd':
        case 'i': {
            int val = va_arg(ap, int);
            written += emit_num(emit, ctx, (unsigned int)val, 10, 1, width, pad_zero);
            break;
        }
        case 'u': {
            unsigned int val = va_arg(ap, unsigned int);
            written += emit_num(emit, ctx, val, 10, 0, width, pad_zero);
            break;
        }
        case 'x': {
            unsigned int val = va_arg(ap, unsigned int);
            written += emit_num(emit, ctx, val, 16, 0, width, pad_zero);
            break;
        }
        case 'p': {
            unsigned int val = (unsigned int)(uintptr_t)va_arg(ap, void *);
            emit('0', ctx);
            emit('x', ctx);
            written += 2 + emit_num(emit, ctx, val, 16, 0, 8, 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            while (*s) {
                emit(*s, ctx);
                written++;
                s++;
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            emit(c, ctx);
            written++;
            break;
        }
        case '%':
            emit('%', ctx);
            written++;
            break;
        default:
            /* 未知格式,原样输出 */
            emit('%', ctx);
            emit(*fmt, ctx);
            written += 2;
            break;
        }
        fmt++;
    }

    return written;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct buf_ctx ctx = {buf, size, 0};
    int            n   = do_printf(buf_emit, &ctx, fmt, ap);
    if (buf && size > 0) {
        size_t end = ctx.pos < size ? ctx.pos : size - 1;
        buf[end]   = '\0';
    }
    return n;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    return do_printf(file_emit, f, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return ret;
}

int putchar(int c) {
    _file_putc(stdout, (char)c);
    return c;
}

int fputc(int c, FILE *f) {
    return _file_putc(f, (char)c);
}

int puts(const char *s) {
    while (*s) {
        _file_putc(stdout, *s++);
    }
    _file_putc(stdout, '\n');
    return 0;
}

int fputs(const char *s, FILE *f) {
    while (*s) {
        _file_putc(f, *s++);
    }
    return 0;
}

int fflush(FILE *stream) {
    if (stream == NULL) {
        _file_flush(stdout);
        _file_flush(stderr);
        return 0;
    }
    return _file_flush(stream);
}
