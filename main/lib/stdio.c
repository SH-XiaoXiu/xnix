/**
 * @file stdio.c
 * @brief 内核标准输出实现
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <drivers/console.h>

#include <xnix/stdio.h>
#include <xnix/types.h>

void kputc(char c) {
    if (c == '\n') {
        console_putc('\r');
    }
    console_putc(c);
}

void kputs(const char *str) {
    if (!str) {
        return;
    }
    while (*str) {
        kputc(*str++);
    }
}

static void print_uint(uint32_t num, int base) {
    static const char digits[] = "0123456789abcdef";
    char              buf[32];
    int               i = 0;

    if (num == 0) {
        kputc('0');
        return;
    }

    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }

    while (i > 0) {
        kputc(buf[--i]);
    }
}

static void print_int(int32_t num) {
    if (num < 0) {
        kputc('-');
        num = -num;
    }
    print_uint((uint32_t)num, 10);
}

static inline void print_hex_padded(uint32_t num, int width) {
    static const char digits[] = "0123456789abcdef";
    for (int i = width - 1; i >= 0; i--) {
        kputc(digits[(num >> (i * 4)) & 0xf]);
    }
}

void vkprintf(const char *fmt, __builtin_va_list args) {
    if (!fmt) {
        return;
    }

    while (*fmt) {
        if (*fmt != '%') {
            kputc(*fmt++);
            continue;
        }

        fmt++;
        switch (*fmt) {
        case 's':
            kputs(__builtin_va_arg(args, const char *) ?: "(null)");
            break;
        case 'c':
            kputc((char)__builtin_va_arg(args, int));
            break;
        case 'd':
        case 'i':
            print_int(__builtin_va_arg(args, int32_t));
            break;
        case 'u':
            print_uint(__builtin_va_arg(args, uint32_t), 10);
            break;
        case 'x':
            print_uint(__builtin_va_arg(args, uint32_t), 16);
            break;
        case 'p':
            kputs("0x");
            print_hex_padded((uint32_t)(uintptr_t)__builtin_va_arg(args, void *), 8);
            break;
        case '%':
            kputc('%');
            break;
        /* 颜色格式符 */
        case 'K':
            console_set_color(KCOLOR_BLACK);
            break;
        case 'R':
            console_set_color(KCOLOR_RED);
            break;
        case 'G':
            console_set_color(KCOLOR_GREEN);
            break;
        case 'Y':
            console_set_color(KCOLOR_YELLOW);
            break;
        case 'B':
            console_set_color(KCOLOR_BLUE);
            break;
        case 'M':
            console_set_color(KCOLOR_MAGENTA);
            break;
        case 'C':
            console_set_color(KCOLOR_CYAN);
            break;
        case 'W':
            console_set_color(KCOLOR_WHITE);
            break;
        case 'N':
            console_reset_color();
            break;
        default:
            kputc('%');
            kputc(*fmt);
            break;
        }
        fmt++;
    }
}

void kprintf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vkprintf(fmt, args);
    __builtin_va_end(args);
}

void klog(int level, const char *fmt, ...) {
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
        console_set_color(KCOLOR_BLUE);
        kputs("[DBG] ");
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
}
