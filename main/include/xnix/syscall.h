#ifndef XNIX_SYSCALL_H
#define XNIX_SYSCALL_H

/**
 * 系统调用号定义
 *
 * 约定:
 * - x86: eax 为 syscall number
 * - 返回值通过 eax 返回
 */
#define SYS_PUTC 1
#define SYS_EXIT 2
#define SYS_ENDPOINT_CREATE 3
#define SYS_IPC_SEND        4
#define SYS_IPC_RECV        5
#define SYS_IPC_CALL        6
#define SYS_IPC_REPLY       7

#endif
