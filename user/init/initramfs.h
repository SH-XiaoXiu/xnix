/**
 * @file initramfs.h
 * @brief Initramfs 提取器
 *
 * 负责从 initramfs.img (FAT12 镜像) 提取文件到内存文件系统.
 */

#ifndef INITRAMFS_H
#define INITRAMFS_H

#include "ramfs.h"

/**
 * 从 initramfs.img 提取文件到 ramfs
 *
 * @param ctx       ramfs 上下文
 * @param img_addr  镜像起始地址
 * @param img_size  镜像大小
 * @return 0 成功, <0 失败
 */
int initramfs_extract(struct ramfs_ctx *ctx, const void *img_addr, uint32_t img_size);

#endif /* INITRAMFS_H */
