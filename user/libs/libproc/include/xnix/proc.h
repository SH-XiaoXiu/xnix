/**
 * @file proc.h
 * @brief 进程创建 Builder API
 *
 * 提供简洁的进程创建接口,封装 abi_exec_args 的构造.
 */

#ifndef XNIX_PROC_H
#define XNIX_PROC_H

#include <xnix/abi/process.h>

struct proc_builder {
    struct abi_exec_args args;
};

/**
 * 初始化 builder
 * @param b    builder 实例
 * @param path 可执行文件路径
 */
void proc_init(struct proc_builder *b, const char *path);

/**
 * 设置 profile
 */
void proc_set_profile(struct proc_builder *b, const char *profile);

/**
 * 设置继承 stdio handles
 */
void proc_inherit_stdio(struct proc_builder *b);

/**
 * 设置继承所有有名称的 handles
 */
void proc_inherit_named(struct proc_builder *b);

/**
 * 设置继承所有 handles
 */
void proc_inherit_all(struct proc_builder *b);

/**
 * 设置继承父进程权限
 */
void proc_inherit_perm(struct proc_builder *b);

/**
 * 显式添加一个 handle
 */
void proc_add_handle(struct proc_builder *b, handle_t src, const char *name);

/**
 * 添加一个参数
 */
void proc_add_arg(struct proc_builder *b, const char *arg);

/**
 * 创建进程
 * @return pid > 0 成功, < 0 错误码
 */
int proc_spawn(struct proc_builder *b);

#endif /* XNIX_PROC_H */
