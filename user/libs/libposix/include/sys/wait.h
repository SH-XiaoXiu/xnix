#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <xnix/abi/process.h>

/*
 * POSIX 风格等待接口。
 *
 * 当前直接建立在内核 waitpid 机制之上：
 * - `waitpid`：精确等待目标 pid
 * - `wait`：等待任意子进程
 */

int waitpid(int pid, int *status, int options);
int wait(int *status);

#endif /* _SYS_WAIT_H */
