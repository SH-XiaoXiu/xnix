/**
 * @file kernel/sys/syscall.h
 * @brief 系统调用表和注册接口
 */

#ifndef KERNEL_SYS_SYSCALL_H
#define KERNEL_SYS_SYSCALL_H

#include <xnix/types.h>

/**
 * 系统调用处理函数类型
 *
 * @param args 参数数组
 * @return 系统调用返回值
 */
typedef int32_t (*syscall_fn_t)(const uint32_t *args);

/**
 * 系统调用表项
 */
struct syscall_entry {
    syscall_fn_t handler; /* 处理函数 */
    uint8_t      nargs;   /* 参数个数(用于调试) */
    const char  *name;    /* 名称(用于调试) */
};

#define NR_SYSCALLS 512

/**
 * 注册系统调用
 *
 * @param nr      系统调用号
 * @param handler 处理函数
 * @param nargs   参数个数
 * @param name    名称
 */
void syscall_register(uint32_t nr, syscall_fn_t handler, uint8_t nargs, const char *name);

/**
 * 初始化系统调用子系统
 */
void syscall_init(void);

#endif /* KERNEL_SYS_SYSCALL_H */
