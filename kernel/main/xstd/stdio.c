/**
 * @file stdio.c
 * @brief 内核标准输出实现
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <xstd/stdio.h>
#include <xstd/stdint.h>

/* weak 默认实现，架构层覆盖 */
__attribute__((weak)) void arch_putc(char c) { (void)c; }
__attribute__((weak)) void arch_console_init(void) {}

void kputc(char c) {
    if (c == '\n') arch_putc('\r');
    arch_putc(c);
}

void kputs(const char* str) {
    if (!str) return;
    while (*str) kputc(*str++);
}

void klog(const char* str) {
    kputs(str);
}

static void print_uint(uint32_t num, int base) {
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int i = 0;

    if (num == 0) {
        kputc('0');
        return;
    }

    while (num > 0) {
        buf[i++] = digits[num % base];
        num /= base;
    }

    while (i > 0) kputc(buf[--i]);
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

void kprintf(const char* fmt, ...) {
    if (!fmt) return;

    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            kputc(*fmt++);
            continue;
        }

        fmt++;
        switch (*fmt) {
            case 's': kputs(__builtin_va_arg(args, const char*) ?: "(null)"); break;
            case 'c': kputc((char)__builtin_va_arg(args, int)); break;
            case 'd':
            case 'i': print_int(__builtin_va_arg(args, int32_t)); break;
            case 'u': print_uint(__builtin_va_arg(args, uint32_t), 10); break;
            case 'x': print_uint(__builtin_va_arg(args, uint32_t), 16); break;
            case 'p': kputs("0x"); print_hex_padded((uint32_t)(uintptr_t)__builtin_va_arg(args, void*), 8); break;
            case '%': kputc('%'); break;
            default:  kputc('%'); kputc(*fmt); break;
        }
        fmt++;
    }

    __builtin_va_end(args);
}
