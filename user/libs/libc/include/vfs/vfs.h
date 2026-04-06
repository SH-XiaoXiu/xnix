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
    int (*open)(void *ctx, const char *path, uint32_t flags);
    int (*close)(void *ctx, uint32_t handle);
    int (*read)(void *ctx, uint32_t handle, void *buf, uint32_t offset, uint32_t size);
    int (*write)(void *ctx, uint32_t handle, const void *buf, uint32_t offset, uint32_t size);
    int (*info)(void *ctx, const char *path, struct vfs_info *info);
    int (*finfo)(void *ctx, uint32_t handle, struct vfs_info *info);
    int (*opendir)(void *ctx, const char *path);
    int (*readdir)(void *ctx, uint32_t handle, uint32_t index, struct vfs_dirent *entry);
    int (*mkdir)(void *ctx, const char *path);
    int (*del)(void *ctx, const char *path);
    int (*truncate)(void *ctx, uint32_t handle, uint64_t new_size);
    int (*sync)(void *ctx, uint32_t handle);
    int (*rename)(void *ctx, const char *old_path, const char *new_path);
};

int vfs_dispatch(struct vfs_operations *ops, void *ctx, struct ipc_message *msg);

#endif /* XNIX_VFS_DISPATCH_H */
