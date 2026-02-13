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

#endif /* XNIX_ENV_H */
