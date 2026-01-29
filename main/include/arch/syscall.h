/**
 * @file arch/syscall.h
 * @brief 平台无关的系统调用参数抽象
 *
 * 各架构实现参数提取和结果设置,内核通过统一接口处理系统调用.
 */

#ifndef ARCH_SYSCALL_H
#define ARCH_SYSCALL_H

#include <xnix/types.h>

#define SYSCALL_MAX_ARGS 6

/**
 * 系统调用参数(由架构层从寄存器提取)
 */
struct syscall_args {
    uint32_t nr;                    /* 系统调用号 */
    uint32_t arg[SYSCALL_MAX_ARGS]; /* 参数 0-5 */
};

/**
 * 系统调用返回值
 */
struct syscall_result {
    int32_t retval;
};

/**
 * 系统调用分发入口(平台无关)
 *
 * @param args 系统调用参数
 * @return 系统调用结果
 */
struct syscall_result syscall_dispatch(const struct syscall_args *args);

#endif /* ARCH_SYSCALL_H */
