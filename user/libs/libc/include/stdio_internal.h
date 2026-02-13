/**
 * @file stdio_internal.h
 * @brief FILE 流内部结构
 *
 * 用户态 I/O 通过统一 fd 层.
 */

#ifndef _STDIO_INTERNAL_H
#define _STDIO_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

#define STREAM_BUF_SIZE 256

/* 缓冲模式 */
enum {
    _IONBF = 0, /* 无缓冲 (stderr) */
    _IOLBF = 1, /* 行缓冲 (stdout) */
    _IOFBF = 2, /* 全缓冲 */
};

/* 流标志 */
#define _FILE_READ  1
#define _FILE_WRITE 2

struct _FILE {
    int  fd; /* 底层 fd (替代原先的 tty_ep) */
    char buf[STREAM_BUF_SIZE];
    int  buf_pos;
    int  buf_mode; /* _IONBF, _IOLBF, _IOFBF */
    int  flags;    /* _FILE_READ / _FILE_WRITE */
    int  error;
    int  eof;
};

/**
 * 初始化标准流
 *
 * 在 __libc_init 中调用,fd 表已由 fd_table_init() 建立,
 * 这里设置 stdin/stdout/stderr FILE 结构.
 */
void _libc_stdio_init(void);

/**
 * 刷新 FILE 流
 */
int _file_flush(FILE *f);

/**
 * 向 FILE 流写入一个字符
 */
int _file_putc(FILE *f, char c);

/**
 * 从 FILE 流读取一个字符
 */
int _file_getc(FILE *f);

#endif /* _STDIO_INTERNAL_H */
