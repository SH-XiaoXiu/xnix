/**
 * @file handle.h
 * @brief Handle ABI 定义
 *
 * 定义了 Handle 类型,对象类型枚举以及 Spawn 相关的结构体.
 * 这些定义在用户态和内核态之间共享.
 */

#ifndef XNIX_ABI_HANDLE_H
#define XNIX_ABI_HANDLE_H

#include <xnix/abi/types.h>

/**
 * @brief Handle 类型定义
 *
 * 用户态看到的 handle 就是一个 32 位整数索引.
 */
typedef uint32_t handle_t;
#define HANDLE_INVALID ((handle_t) - 1)

/**
 * @brief Handle 对象类型枚举
 */
typedef enum {
    HANDLE_NONE         = 0,
    HANDLE_ENDPOINT     = 1, /* IPC endpoint */
    HANDLE_PHYSMEM      = 2, /* 物理内存区域 */
    HANDLE_NOTIFICATION = 3, /* 异步通知 */
    HANDLE_VMAR         = 4, /* 虚拟内存区域(预留) */
    HANDLE_THREAD       = 5, /* 线程 */
    HANDLE_PROCESS      = 6, /* 进程 */
} handle_type_t;

/**
 * @brief Spawn 时传递的 Handle 描述符
 *
 * 用于 process_spawn 系统调用,指示如何将父进程的 handle
 * 传递给子进程.
 */
struct spawn_handle {
    handle_t src;      /* 父进程中的 handle */
    char     name[16]; /* 子进程中的 handle 名称 */
};

#endif /* XNIX_ABI_HANDLE_H */
