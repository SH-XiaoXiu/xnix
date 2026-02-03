/**
 * @file vfs.h
 * @brief VFS 协议解析库
 *
 * 提供 VFS 操作接口定义和消息分发函数
 * 驱动使用 libudm 启动服务,在 handler 中调用 vfs_dispatch()
 */

#ifndef VFS_VFS_H
#define VFS_VFS_H

#include <d/protocol/vfs.h>
#include <stdint.h>
#include <xnix/ipc.h>

/**
 * 文件系统操作接口
 *
 * 返回值:成功 >= 0,失败返回负数错误码
 */
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

/**
 * 分发 VFS 消息到对应的操作回调
 *
 * @param ops 文件系统操作接口
 * @param ctx 传递给回调的上下文(如 ramfs 的文件树指针)
 * @param msg IPC 消息
 * @return 0 成功,负数失败
 */
int vfs_dispatch(struct vfs_operations *ops, void *ctx, struct ipc_message *msg);

#endif /* VFS_VFS_H */
