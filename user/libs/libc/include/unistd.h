/**
 * @file unistd.h
 * @brief POSIX-like unistd.h
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <xnix/syscall.h>

/* ssize_t */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int32_t ssize_t;
#endif

/* 标准文件描述符 */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/**
 * 睡眠指定秒数
 */
static inline unsigned int sleep(unsigned int seconds) {
    sys_sleep(seconds * 1000);
    return 0;
}

/**
 * 睡眠指定毫秒数
 */
static inline int msleep(unsigned int ms) {
    sys_sleep(ms);
    return 0;
}

/* POSIX I/O */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
int     dup(int oldfd);
int     dup2(int oldfd, int newfd);
int     pipe(int pipefd[2]);

#endif /* _UNISTD_H */
