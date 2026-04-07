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
    char     path[VFS_PATH_MAX];
    uint32_t flags;
    handle_t file_ep; /* 兼容字段: fatfs 现改为 main_ep + session 模型 */
    uint16_t generation;
    uint8_t  type;    /* 0=file, 1=dir */
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

/**
 * 处理文件会话上的 IO 消息 (IO_READ/IO_WRITE/IO_CLOSE 等)
 */
int fatfs_file_ep_dispatch(struct fatfs_ctx *ctx, int slot, struct ipc_message *msg);

/**
 * 获取指定 slot 的 file_ep handle
 */
handle_t fatfs_get_file_ep(struct fatfs_ctx *ctx, int slot);

#endif /* FATFS_VFS_H */
