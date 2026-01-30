/**
 * @file unistd.h
 * @brief POSIX-like unistd.h
 */

#ifndef _UNISTD_H
#define _UNISTD_H

#include <xnix/syscall.h>

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

#endif /* _UNISTD_H */
