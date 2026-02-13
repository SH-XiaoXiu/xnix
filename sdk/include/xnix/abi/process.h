/**
 * @file abi/process.h
 * @brief 进程相关 ABI 定义
 */

#ifndef XNIX_ABI_PROCESS_H
#define XNIX_ABI_PROCESS_H

#include <xnix/abi/handle.h>
#include <xnix/abi/stdint.h>

#define ABI_SPAWN_PROFILE_LEN 32

/*
 * Handle 继承标志(用于 abi_exec_args.flags / abi_exec_image_args.flags)
 */
#define ABI_EXEC_INHERIT_NONE    0x00  /* 默认:仅传递 handles[] 中显式列举的 */
#define ABI_EXEC_INHERIT_STDIO   0x01  /* 自动继承 stdin/stdout/stderr */
#define ABI_EXEC_INHERIT_NAMED   0x02  /* 继承父进程所有有名称的 handle */
#define ABI_EXEC_INHERIT_ALL     0x04  /* 继承父进程所有 handle */
#define ABI_EXEC_INHERIT_PERM    0x08  /* 继承父进程权限(忽略 profile_name)*/

/*
 * exec 系统调用参数限制
 */
#define ABI_EXEC_MAX_ARGS    16  /* 最大参数数量 */
#define ABI_EXEC_MAX_ARG_LEN 256 /* 单个参数最大长度 */
#define ABI_EXEC_PATH_MAX    256 /* 路径最大长度 */
#define ABI_EXEC_MAX_HANDLES 16  /* 最大传递 handle 数量 */
#define ABI_PROC_NAME_MAX    16  /* 进程名最大长度 */

/**
 * @brief exec 系统调用参数结构
 */
struct abi_exec_args {
    char                path[ABI_EXEC_PATH_MAX];                       /* 可执行文件路径 */
    char                profile_name[ABI_SPAWN_PROFILE_LEN];           /* 权限 profile 名称 */
    int32_t             argc;                                          /* 参数数量 */
    char                argv[ABI_EXEC_MAX_ARGS][ABI_EXEC_MAX_ARG_LEN]; /* 参数数组 */
    uint32_t            flags;                                         /* 执行标志(保留) */
    uint32_t            handle_count;                                  /* 传递的 handle 数量 */
    struct spawn_handle handles[ABI_EXEC_MAX_HANDLES];                 /* 传递的 handles */
};

/**
 * @brief exec_image 系统调用参数结构
 */
struct abi_exec_image_args {
    char                name[ABI_PROC_NAME_MAX];                       /* 进程名称 */
    char                profile_name[ABI_SPAWN_PROFILE_LEN];           /* 权限 profile 名称 */
    uint32_t            elf_ptr;                                       /* ELF 镜像地址 */
    uint32_t            elf_size;                                      /* ELF 镜像大小 */
    int32_t             argc;                                          /* 参数数量 */
    char                argv[ABI_EXEC_MAX_ARGS][ABI_EXEC_MAX_ARG_LEN]; /* 参数数组 */
    uint32_t            flags;                                         /* 执行标志 */
    uint32_t            handle_count;                                  /* 传递的 handle 数量 */
    struct spawn_handle handles[ABI_EXEC_MAX_HANDLES];                 /* 传递的 handles */
};

/*
 * proclist 系统调用相关定义
 */
#define ABI_PROCLIST_MAX 64 /* 单次返回最多 64 条 */

/**
 * @brief 进程信息结构(用于用户态获取进程列表)
 */
struct abi_proc_info {
    int32_t  pid;                     /* 进程 ID */
    int32_t  ppid;                    /* 父进程 ID */
    uint8_t  state;                   /* 0=RUNNING, 1=ZOMBIE */
    uint8_t  reserved[3];             /* 保留 */
    uint32_t thread_count;            /* 线程数量 */
    uint64_t cpu_ticks;               /* 累计 CPU ticks(所有线程总和) */
    uint32_t heap_kb;                 /* 堆内存(KB) */
    uint32_t stack_kb;                /* 栈内存(KB) */
    char     name[ABI_PROC_NAME_MAX]; /* 进程名 */
};

/**
 * @brief 系统信息结构
 */
struct abi_sys_info {
    uint32_t cpu_count;   /* CPU 数量 */
    uint64_t total_ticks; /* 全局 tick 计数 */
    uint64_t idle_ticks;  /* idle tick 计数 */
};

/**
 * @brief proclist 系统调用参数
 */
struct abi_proclist_args {
    struct abi_proc_info *buf;         /* 用户缓冲区 */
    uint32_t              buf_count;   /* 缓冲区可容纳条目数 */
    uint32_t              start_index; /* 起始索引(用于分页) */
    struct abi_sys_info  *sys_info;    /* 系统信息输出(可为 NULL) */
};

#define WNOHANG 1

#endif /* XNIX_ABI_PROCESS_H */
