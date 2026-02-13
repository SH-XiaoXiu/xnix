/**
 * @file fd.h
 * @brief 统一文件描述符表
 *
 * 提供 int fd -> { handle, type, state } 的统一映射,
 * 替代之前 VFS 独立 fd 表和 stdio 直持 handle 的分裂模式.
 */

#ifndef _XNIX_FD_H
#define _XNIX_FD_H

#include <stdint.h>
#include <xnix/abi/handle.h>

#define FD_MAX 64

/* fd 类型 */
#define FD_TYPE_NONE 0
#define FD_TYPE_TTY  1 /* TTY endpoint -- 走 TTY IPC 协议 */
#define FD_TYPE_VFS  2 /* VFS file -- 走 VFS IPC 协议 */
#define FD_TYPE_PIPE 3 /* Pipe endpoint -- 简单 IPC send/recv */

/* fd 标志 */
#define FD_FLAG_READ    0x01
#define FD_FLAG_WRITE   0x02
#define FD_FLAG_CLOEXEC 0x04

/* VFS 特有状态 */
struct fd_vfs_state {
    uint32_t fs_handle; /* FS driver 内部 handle */
    uint32_t fs_ep;     /* FS driver endpoint */
    uint32_t offset;    /* 文件偏移 */
    uint32_t flags;     /* VFS open flags */
};

struct fd_entry {
    handle_t            handle; /* 内核 handle 索引 */
    uint8_t             type;   /* FD_TYPE_* */
    uint8_t             flags;  /* FD_FLAG_* */
    struct fd_vfs_state vfs;    /* type==FD_TYPE_VFS 时有效 */
};

/**
 * 初始化 fd 表
 *
 * 清零 fd 表,查找 stdin/stdout/stderr handle 并填入 fd 0/1/2.
 */
void fd_table_init(void);

/**
 * 分配最小可用 fd
 *
 * @return fd >= 0 成功, -1 无可用 fd
 */
int fd_alloc(void);

/**
 * 分配指定 fd (dup2 用)
 *
 * 如果 fd 已占用,先释放再分配.
 *
 * @param fd 目标 fd
 * @return 0 成功, -1 失败
 */
int fd_alloc_at(int fd);

/**
 * 释放 fd (不关闭内核 handle)
 *
 * @param fd 要释放的 fd
 */
void fd_free(int fd);

/**
 * 获取 fd 条目
 *
 * @param fd 文件描述符
 * @return fd_entry 指针,无效时返回 NULL
 */
struct fd_entry *fd_get(int fd);

/**
 * 获取 fd 对应的内核 handle
 *
 * @param fd 文件描述符
 * @return handle 值,无效时返回 HANDLE_INVALID
 */
handle_t fd_get_handle(int fd);

/**
 * 安装 fd 条目
 *
 * 将 handle/type/flags 写入指定 fd slot.
 * 如果 fd 已占用,覆盖(调用者负责先 close).
 *
 * @param fd     目标 fd
 * @param handle 内核 handle
 * @param type   FD_TYPE_*
 * @param flags  FD_FLAG_*
 * @return fd_entry 指针供调用者设置 VFS 状态, NULL=fd 越界
 */
struct fd_entry *fd_install(int fd, handle_t handle, uint8_t type, uint8_t flags);

#endif /* _XNIX_FD_H */
