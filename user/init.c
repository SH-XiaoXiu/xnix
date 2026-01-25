
/* 系统调用号 (必须与内核保持一致) */
#define SYS_PUTC 1
#define SYS_EXIT 2

/* 系统调用接口 */
static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

void sys_putc(char c) {
    syscall1(SYS_PUTC, c);
}

void sys_exit(int code) {
    syscall1(SYS_EXIT, code);
    while (1) {
    }
}

#include <stdarg.h>

// 辅助函数：打印整数
static void print_int(int val, int base, int is_signed) {
    char         buf[32];
    int          i = 0;
    unsigned int uval;

    if (is_signed && val < 0) {
        sys_putc('-');
        uval = (unsigned int)(-val);
    } else {
        uval = (unsigned int)val;
    }

    if (uval == 0) {
        sys_putc('0');
        return;
    }

    while (uval > 0) {
        int digit = uval % base;
        buf[i++]  = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        uval /= base;
    }

    while (i--) {
        sys_putc(buf[i]);
    }
}

void printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    const char *p = fmt;
    while (*p) {
        if (*p == '%') {
            p++;
            switch (*p) {
            case 's': {
                const char *s = va_arg(args, const char *);
                while (*s) {
                    sys_putc(*s++);
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int); // char 会被提升为 int
                sys_putc(c);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                print_int(d, 10, 1);
                break;
            }
            case 'x': {
                int x = va_arg(args, int);
                print_int(x, 16, 0);
                break;
            }
            case '%': {
                sys_putc('%');
                break;
            }
            default:
                sys_putc('%');
                sys_putc(*p);
                break;
            }
        } else {
            sys_putc(*p);
        }
        p++;
    }

    va_end(args);
}

int main(void) {
    printf("I am running via ELF Loader!\n");
    int i = 0;
    while (1) {
        printf("Hello from C User Program!\n");
        printf("Counting: %d\n", i);
        i++;
        /* 忙等待延时 */
        for (volatile int j = 0; j < 50000000; j++);
    }
    printf("Exiting...\n");
    return 0;
}
