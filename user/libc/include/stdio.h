/**
 * @file stdio.h
 * @brief 标准 I/O 函数
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

/* 格式化输出 */
int printf(const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* 字符输出 */
int putchar(int c);
int puts(const char *s);

/* 字符输入 */
int getchar(void);

/* 流操作 */
int fflush(void *stream);

#endif /* _STDIO_H */
