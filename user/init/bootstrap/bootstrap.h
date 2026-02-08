/**
 * @file bootstrap.h
 * @brief Init 自举接口定义
 *
 * 提供不依赖外部服务的最小启动支持:
 * - 直接 exec 封装(绕过 VFS, 从内存中的 ELF 启动进程)
 */

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xnix/abi/handle.h>

/**
 * 直接执行 ELF(绕过 VFS)
 *
 * @param elf_data ELF 文件数据
 * @param elf_size ELF 文件大小
 * @param name 进程名
 * @param argv 参数数组(可为 NULL)
 * @param handles handle 传递数组
 * @param handle_count handle 数量
 * @param profile_name 权限 profile 名称
 * @return 进程 PID,负数失败
 */
struct spawn_handle;
int bootstrap_exec(const void *elf_data, size_t elf_size, const char *name, char **argv,
                   const struct spawn_handle *handles, int handle_count, const char *profile_name);

#endif /* BOOTSTRAP_H */
