/**
 * @file vfs.h
 * @brief VFS 服务端分发辅助
 */

#ifndef XNIX_VFS_DISPATCH_H
#define XNIX_VFS_DISPATCH_H

#include <stdint.h>
#include <xnix/ipc.h>
#include <xnix/protocol/vfs.h>

struct vfs_operations {
    /* 命名空间操作(路径层) */
    int (*open)(void *ctx, const char *path, uint32_t flags, handle_t *out_ep);
    int (*close)(void *ctx, uint32_t handle);  /* 目录 handle 关闭(文件 close 走 file_ep) */
    int (*info)(void *ctx, const char *path, struct vfs_info *info);
    int (*opendir)(void *ctx, const char *path);
    int (*readdir)(void *ctx, uint32_t handle, uint32_t index, struct vfs_dirent *entry);
    int (*mkdir)(void *ctx, const char *path);
    int (*del)(void *ctx, const char *path);
    int (*rename)(void *ctx, const char *old_path, const char *new_path);

    /* 文件 IO (read/write/finfo/truncate/sync) 已从接口移除,
       由各 backend 在各自 file_ep 的 event loop 中直接处理 IO_READ/IO_WRITE/IO_CLOSE */
};

int vfs_dispatch(struct vfs_operations *ops, void *ctx, struct ipc_message *msg);

#endif /* XNIX_VFS_DISPATCH_H */
