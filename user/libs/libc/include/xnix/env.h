/**
 * @file env.h
 * @brief 环境变量和 handle 查找接口
 */

#ifndef XNIX_ENV_H
#define XNIX_ENV_H

#include <stdint.h>
#include <xnix/abi/handle.h>

/**
 * 初始化环境 handle 映射(由 libc 启动代码调用)
 *
 * @param handles handle 数组
 * @param count   handle 数量
 */
void __env_init_handles(const char **handle_names, uint32_t *handle_values, int count);

/**
 * 根据名称查找 handle
 *
 * @param name handle 名称
 * @return handle 值,未找到返回 HANDLE_INVALID
 */
uint32_t env_get_handle(const char *name);

/**
 * 设置进程显示名(用于 env_require 的错误消息)
 */
void env_set_name(const char *name);

/**
 * 获取必需 handle,失败时自动打印诊断信息
 *
 * @param name handle 名称
 * @return handle 值,未找到返回 HANDLE_INVALID(已打印错误)
 */
uint32_t env_require(const char *name);

/**
 * 查找命名 handle 并映射其物理内存
 *
 * @param name     handle 名称
 * @param out_size 可选输出: 映射大小
 * @return 映射地址,失败返回 NULL(已打印错误)
 */
void *env_mmap_resource(const char *name, uint32_t *out_size);

#endif /* XNIX_ENV_H */
