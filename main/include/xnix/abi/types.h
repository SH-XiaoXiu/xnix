/**
 * @file abi/types.h
 * @brief ABI 类型定义
 *
 * 内核-用户态契约类型.不可更改.
 */

#ifndef XNIX_ABI_TYPES_H
#define XNIX_ABI_TYPES_H

#include <xnix/abi/stdint.h>

/*
 * 进程/线程 ID
 *
 * 使用有符号类型,负数表示错误或无效
 */
typedef int32_t pid_t;
typedef int32_t tid_t;
#define PID_INVALID ((pid_t) - 1)
#define TID_INVALID ((tid_t) - 1)

#endif /* XNIX_ABI_TYPES_H */
