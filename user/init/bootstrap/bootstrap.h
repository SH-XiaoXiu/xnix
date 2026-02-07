/**
 * @file bootstrap.h
 * @brief Init 自举接口定义
 *
 * 提供不依赖外部服务的最小启动支持:
 * - TAR 解析器(复用 initramfs_tar.c)
 * - FAT32 只读驱动
 * - 直接 exec 封装(绕过 VFS)
 */

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xnix/abi/handle.h>

/* FAT32 卷 */
typedef struct fat32_volume fat32_volume_t;

/**
 * 挂载 FAT32 卷
 *
 * @param data FAT32 镜像数据
 * @param size 镜像大小
 * @return FAT32 卷对象,失败返回 NULL
 */
fat32_volume_t *fat32_mount(const void *data, size_t size);

/**
 * 从 FAT32 卷读取文件
 *
 * @param vol FAT32 卷
 * @param path 文件路径(不支持长文件名,8.3 格式)
 * @param out_data 输出文件数据指针
 * @param out_size 输出文件大小
 * @return 0 成功,负数失败
 */
int fat32_open(fat32_volume_t *vol, const char *path, const void **out_data, size_t *out_size);

/**
 * 关闭 FAT32 卷
 *
 * @param vol FAT32 卷
 */
void fat32_close(fat32_volume_t *vol);

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
