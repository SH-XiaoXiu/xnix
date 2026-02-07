/**
 * @file stdio.h
 * @brief 标准 I/O 函数
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <xnix/abi/handle.h>

/* EOF */
#ifndef EOF
#define EOF (-1)
#endif

/* FILE 类型 (opaque) */
typedef struct _FILE FILE;

/* 标准流 */
extern FILE _stdin_file, _stdout_file, _stderr_file;
#define stdin  (&_stdin_file)
#define stdout (&_stdout_file)
#define stderr (&_stderr_file)

/* 格式化输出 */
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);

/* 字符/字符串输出 */
int putchar(int c);
int fputc(int c, FILE *f);
int puts(const char *s);
int fputs(const char *s, FILE *f);

/* 字符/字符串输入 */
int   getchar(void);
int   fgetc(FILE *f);
char *gets_s(char *buf, size_t size);

/* 流操作 */
int fflush(FILE *stream);

/**
 * 强制 stdout/stderr 使用 SYS_DEBUG_WRITE fallback
 *
 * 用于 ttyd 等特殊服务,避免 printf 发给自己死锁.
 */
void _stdio_force_debug_mode(void);

/**
 * 设置 FILE 流的 TTY endpoint
 *
 * @param f      FILE 流
 * @param tty_ep TTY endpoint handle
 */
void _stdio_set_tty(FILE *f, handle_t tty_ep);

#endif /* _STDIO_H */
