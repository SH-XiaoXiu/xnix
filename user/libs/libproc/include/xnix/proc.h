/**
 * @file proc.h
 * @brief 进程创建 Builder API
 *
 * 提供进程创建 builder,封装 abi_exec_args / abi_exec_image_args 的构造.
 *
 * 注意:
 * - 这是系统管理/引导辅助层，不是最终的普通应用进程 API。
 * - 面向普通用户程序的高层 spawn/exec/fork 语义应逐步迁到 libposix。
 * - 仍需要显式构造 handle/stdio/image 参数的程序才应直接依赖本头。
 *
 * TODO(userland layering):
 *   继续把普通应用可见的高层进程接口迁到 libposix，
 *   让 libproc 最终只保留：
 *   - init/bootstrap
 *   - image builder
 *   - 显式 handle/stdio 图构造
 */

#ifndef XNIX_PROC_H
#define XNIX_PROC_H

#include <stddef.h>
#include <xnix/abi/process.h>

/* ===== VFS 路径 builder (abi_exec_args) ===== */

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
 * 初始化 builder + 默认开启 stdio 继承
 */
void proc_new(struct proc_builder *b, const char *path);

/**
 * 设置 flags
 */
void proc_set_flags(struct proc_builder *b, uint32_t flags);

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
 * 添加 handle 并指定子进程中的 rights
 *
 * @param b      builder 实例
 * @param h      源 handle
 * @param name   子进程中的名称
 * @param rights 子进程中的权限位(0=继承源 handle 的全部权限)
 */
void proc_add_handle_with_rights(struct proc_builder *b, handle_t h,
                                 const char *name, uint32_t rights);

/**
 * 添加一个参数
 */
void proc_add_arg(struct proc_builder *b, const char *arg);

/**
 * 从空格分隔的字符串解析并添加参数
 */
void proc_add_args_string(struct proc_builder *b, const char *args_str);

/**
 * 创建进程
 * @return pid > 0 成功, < 0 错误码
 */
int proc_spawn(struct proc_builder *b);

/**
 * 一行式启动: 继承 stdio + named handles
 */
int proc_spawn_simple(const char *path);

/**
 * 带参数的一行式启动
 */
int proc_spawn_args(const char *path, int argc, const char **argv);

/* ===== 内存 ELF builder (abi_exec_image_args) ===== */

struct proc_image_builder {
    struct abi_exec_image_args args;
};

/**
 * 初始化内存 ELF builder
 */
void proc_image_init(struct proc_image_builder *b, const char *name,
                     const void *elf_data, size_t elf_size);

/**
 * 设置 flags
 */
void proc_image_set_flags(struct proc_image_builder *b, uint32_t flags);

/**
 * 显式添加一个 handle
 */
void proc_image_add_handle(struct proc_image_builder *b, handle_t src, const char *name);

/**
 * 添加一个参数
 */
void proc_image_add_arg(struct proc_image_builder *b, const char *arg);

/**
 * 创建进程
 * @return pid > 0 成功, < 0 错误码
 */
int proc_image_spawn(struct proc_image_builder *b);

#endif /* XNIX_PROC_H */
