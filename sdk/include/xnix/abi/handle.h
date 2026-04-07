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
 * @brief Handle 名称最大长度(包含 '\0')
 */
#define HANDLE_NAME_MAX 16

/**
 * @brief Handle 权限位
 *
 * 每个 handle 携带 rights 位图,控制该 handle 能执行的操作.
 * 传递 handle 时 rights 只能缩小(取交集),不能增加.
 */
#define HANDLE_RIGHT_READ      0x01 /* 可读/可接收 */
#define HANDLE_RIGHT_WRITE     0x02 /* 可写/可发送 */
#define HANDLE_RIGHT_EXECUTE   0x04 /* 可执行(预留) */
#define HANDLE_RIGHT_TRANSFER  0x08 /* 可转移给其他进程 */
#define HANDLE_RIGHT_DUPLICATE 0x10 /* 可复制 */
#define HANDLE_RIGHT_ALL       0x1F /* 全部权限 */

/**
 * @brief Handle 对象类型枚举
 */
typedef enum {
    HANDLE_NONE         = 0,
    HANDLE_ENDPOINT     = 1, /* IPC endpoint */
    HANDLE_PHYSMEM      = 2, /* 物理内存区域 */
    HANDLE_EVENT        = 3, /* 异步事件 */
    HANDLE_VMAR         = 4, /* 虚拟内存区域(预留) */
    HANDLE_THREAD       = 5, /* 线程 */
    HANDLE_PROCESS      = 6, /* 进程 */
    HANDLE_PROC_WATCH   = 7, /* 进程生命周期观察器 */
    HANDLE_PIPE_READ    = 8, /* 管道读端 */
    HANDLE_PIPE_WRITE   = 9, /* 管道写端 */
} handle_type_t;

/**
 * @brief Spawn 时传递的 Handle 描述符
 *
 * 用于 process_spawn 系统调用,指示如何将父进程的 handle
 * 传递给子进程.
 */
struct spawn_handle {
    handle_t src;                   /* 父进程中的 handle */
    char     name[HANDLE_NAME_MAX]; /* 子进程中的 handle 名称 */
    uint32_t rights;                /* 子进程中的 rights, 0 = 继承源 handle 的 rights */
};

/**
 * @brief 标准 Handle 名称
 */
#define HANDLE_STDIO_STDIN   "stdin"
#define HANDLE_STDIO_STDOUT  "stdout"
#define HANDLE_STDIO_STDERR  "stderr"

/**
 * @brief Handle 自省信息(用于 SYS_HANDLE_LIST)
 */
struct abi_handle_info {
    handle_t      handle;               /* handle 值 */
    handle_type_t type;                 /* 对象类型 */
    uint32_t      rights;               /* handle 权限位 */
    char          name[HANDLE_NAME_MAX]; /* handle 名称 */
};

#endif /* XNIX_ABI_HANDLE_H */
