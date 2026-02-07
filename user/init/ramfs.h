/**
 * @file ramfs.h
 * @brief 内存文件系统内部定义
 */

#ifndef RAMFS_H
#define RAMFS_H

#include <stdbool.h>
#include <stdint.h>
#include <vfs/vfs.h>

#define RAMFS_NAME_MAX    255
#define RAMFS_MAX_NODES   256
#define RAMFS_MAX_HANDLES 64

/* 节点类型 */
#define RAMFS_TYPE_FILE 0
#define RAMFS_TYPE_DIR  1

/* 文件/目录节点 */
struct ramfs_node {
    char     name[RAMFS_NAME_MAX + 1];
    uint32_t type;
    uint32_t size;     /* 文件大小 */
    char    *data;     /* 文件内容 */
    uint32_t capacity; /* 已分配容量 */

    struct ramfs_node *parent;
    struct ramfs_node *children; /* 目录的第一个子节点 */
    struct ramfs_node *next;     /* 同级下一个节点 */

    bool in_use;
};

/* 打开的文件句柄 */
struct ramfs_handle {
    struct ramfs_node *node;
    uint32_t           flags;
    bool               in_use;
};

/* 文件系统上下文 */
struct ramfs_ctx {
    struct ramfs_node   nodes[RAMFS_MAX_NODES];
    struct ramfs_handle handles[RAMFS_MAX_HANDLES];
    struct ramfs_node  *root;
};

/**
 * 初始化 ramfs
 */
void ramfs_init(struct ramfs_ctx *ctx);

/**
 * 获取 vfs_operations
 */
struct vfs_operations *ramfs_get_ops(void);

/**
 * 直接操作 ramfs 的函数(用于 init 和 initramfs 提取)
 * 注意:这些函数使用 vfs_operations 签名(void *vctx, uint32_t handle)
 */
int ramfs_mkdir(void *vctx, const char *path);
int ramfs_open(void *vctx, const char *path, uint32_t flags);
int ramfs_close(void *vctx, uint32_t handle);
int ramfs_read(void *vctx, uint32_t handle, void *buf, uint32_t offset, uint32_t size);
int ramfs_write(void *vctx, uint32_t handle, const void *buf, uint32_t offset, uint32_t size);
int ramfs_finfo(void *vctx, uint32_t handle, struct vfs_info *info);

#endif /* RAMFS_H */
