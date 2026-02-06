/**
 * @file fatfs_vfs.h
 * @brief FatFs VFS 接口
 */

#ifndef FATFS_VFS_H
#define FATFS_VFS_H

#include <ff.h>
#include <stdint.h>
#include <vfs/vfs.h>

#define FATFS_MAX_HANDLES 32

/* 打开的文件/目录句柄 */
struct fatfs_handle {
    union {
        FIL file;
        DIR dir;
    } obj;
    uint32_t flags;
    uint8_t  type; /* 0=file, 1=dir */
    uint8_t  in_use;
};

/* FatFs 上下文 */
struct fatfs_ctx {
    FATFS               fs;
    struct fatfs_handle handles[FATFS_MAX_HANDLES];
    uint8_t             mounted;
};

/**
 * 初始化 FatFs
 */
int fatfs_init(struct fatfs_ctx *ctx);

/**
 * 获取 vfs_operations
 */
struct vfs_operations *fatfs_get_ops(void);

#endif /* FATFS_VFS_H */
