/**
 * @file stdio_internal.h
 * @brief FILE 流内部结构
 *
 * 用户态 I/O 通过统一 fd 层.
 * 支持自适应通道: 当 fd 无效时走 SYS_DEBUG_WRITE, fd 有效后自动升级.
 */

#ifndef _STDIO_INTERNAL_H
#define _STDIO_INTERNAL_H

#include <stdint.h>
#include <stdio.h>
#include <xnix/syscall.h>

/**
 * SYS_DEBUG_WRITE fallback
 *
 * pre-TTY 阶段或 ttyd 自身使用,绕过 IPC 直接写内核 early console.
 */
static inline void _debug_write(const void *buf, size_t len) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(SYS_DEBUG_WRITE), "b"((uint32_t)(uintptr_t)buf), "c"((uint32_t)len)
                 : "memory");
    (void)ret;
}

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

/**
 * stdio 输出通道
 *
 * 当 fd 无效时使用 DEBUG 通道 (SYS_DEBUG_WRITE),
 * fd 有效后自动升级为 FD 通道 (write/read syscall).
 * 升级单向: DEBUG -> FD, 不降级.
 */
enum stdio_channel {
    STDIO_CH_NONE  = 0, /* 无输出 */
    STDIO_CH_DEBUG = 1, /* SYS_DEBUG_WRITE 直出内核 */
    STDIO_CH_FD    = 2, /* 正常 fd -> IPC */
};

struct _FILE {
    int                fd;          /* 底层 fd (-1 如果未绑定) */
    enum stdio_channel channel;     /* 当前活跃通道 */
    int                reprobe_cnt; /* reprobe 频率限制计数器 */
    char               buf[STREAM_BUF_SIZE];
    int                buf_pos;
    int                buf_mode;    /* _IONBF, _IOLBF, _IOFBF */
    int                flags;       /* _FILE_READ / _FILE_WRITE */
    int                error;
    int                eof;
};

/**
 * 初始化标准流
 *
 * 在 __libc_init 中调用,fd 表已由 fd_table_init() 建立,
 * 这里设置 stdin/stdout/stderr FILE 结构并确定初始通道.
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
