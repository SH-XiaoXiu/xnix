/**
 * @file printf.c
 * @brief 简易 printf 实现
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>

int putchar(int c) {
    sys_putc((char)c);
    return c;
}

int puts(const char *s) {
    while (*s) {
        putchar(*s++);
    }
    putchar('\n');
    return 0;
}

/* 数字转字符串（内部函数） */
static int print_num(char *buf, size_t size, unsigned int num, int base, int is_signed, int width,
                     int pad_zero) {
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

    /* 补齐宽度 */
    int len = i;
    int pad = (width > len) ? (width - len) : 0;

    int written = 0;
    if (buf) {
        /* 写入缓冲区 */
        while (pad-- > 0 && written < (int)size - 1) {
            buf[written++] = pad_zero ? '0' : ' ';
        }
        while (i-- > 0 && written < (int)size - 1) {
            buf[written++] = tmp[i];
        }
        buf[written] = '\0';
    } else {
        /* 直接输出 */
        while (pad-- > 0) {
            putchar(pad_zero ? '0' : ' ');
            written++;
        }
        while (i-- > 0) {
            putchar(tmp[i]);
            written++;
        }
    }

    return written;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    int    written = 0;
    char  *p       = buf;
    size_t remain  = size;

    while (*fmt) {
        if (*fmt != '%') {
            if (buf) {
                if (remain > 1) {
                    *p++ = *fmt;
                    remain--;
                }
            } else {
                putchar(*fmt);
            }
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
            if (buf) {
                int n = print_num(p, remain, (unsigned int)val, 10, 1, width, pad_zero);
                p += n;
                remain -= n;
                written += n;
            } else {
                written += print_num(NULL, 0, (unsigned int)val, 10, 1, width, pad_zero);
            }
            break;
        }
        case 'u': {
            unsigned int val = va_arg(ap, unsigned int);
            if (buf) {
                int n = print_num(p, remain, val, 10, 0, width, pad_zero);
                p += n;
                remain -= n;
                written += n;
            } else {
                written += print_num(NULL, 0, val, 10, 0, width, pad_zero);
            }
            break;
        }
        case 'x': {
            unsigned int val = va_arg(ap, unsigned int);
            if (buf) {
                int n = print_num(p, remain, val, 16, 0, width, pad_zero);
                p += n;
                remain -= n;
                written += n;
            } else {
                written += print_num(NULL, 0, val, 16, 0, width, pad_zero);
            }
            break;
        }
        case 'p': {
            unsigned int val = (unsigned int)(uintptr_t)va_arg(ap, void *);
            if (buf) {
                if (remain > 2) {
                    *p++ = '0';
                    *p++ = 'x';
                    remain -= 2;
                }
                int n = print_num(p, remain, val, 16, 0, 8, 1);
                p += n;
                remain -= n;
                written += 2 + n;
            } else {
                putchar('0');
                putchar('x');
                written += 2 + print_num(NULL, 0, val, 16, 0, 8, 1);
            }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            while (*s) {
                if (buf) {
                    if (remain > 1) {
                        *p++ = *s;
                        remain--;
                    }
                } else {
                    putchar(*s);
                }
                written++;
                s++;
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (buf) {
                if (remain > 1) {
                    *p++ = c;
                    remain--;
                }
            } else {
                putchar(c);
            }
            written++;
            break;
        }
        case '%':
            if (buf) {
                if (remain > 1) {
                    *p++ = '%';
                    remain--;
                }
            } else {
                putchar('%');
            }
            written++;
            break;
        default:
            /* 未知格式，原样输出 */
            if (buf) {
                if (remain > 1) {
                    *p++ = '%';
                    remain--;
                }
                if (remain > 1) {
                    *p++ = *fmt;
                    remain--;
                }
            } else {
                putchar('%');
                putchar(*fmt);
            }
            written += 2;
            break;
        }
        fmt++;
    }

    if (buf && size > 0) {
        *p = '\0';
    }

    return written;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    return ret;
}
