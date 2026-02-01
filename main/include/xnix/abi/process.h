/**
 * @file abi/process.h
 * @brief 进程相关 ABI 定义
 */

#ifndef XNIX_ABI_PROCESS_H
#define XNIX_ABI_PROCESS_H

#include <xnix/abi/stdint.h>

/*
 * exec 系统调用参数限制
 */
#define ABI_EXEC_MAX_ARGS    16  /* 最大参数数量 */
#define ABI_EXEC_MAX_ARG_LEN 256 /* 单个参数最大长度 */
#define ABI_EXEC_PATH_MAX    256 /* 路径最大长度 */

/**
 * exec 系统调用参数结构
 */
struct abi_exec_args {
    char     path[ABI_EXEC_PATH_MAX];                       /* 可执行文件路径 */
    int32_t  argc;                                          /* 参数数量 */
    char     argv[ABI_EXEC_MAX_ARGS][ABI_EXEC_MAX_ARG_LEN]; /* 参数数组 */
    uint32_t flags;                                         /* 执行标志(保留) */
};

#endif /* XNIX_ABI_PROCESS_H */
