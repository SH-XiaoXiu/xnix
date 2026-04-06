/**
 * @file fd.h
 * @brief 统一文件描述符表
 *
 * fd = { handle, session, offset, flags }
 *
 * handle  = IPC endpoint (对端是谁)
 * session = 服务端 session ID (VFS/TTY/其他对象都可使用, 0 只是合法值)
 * offset  = 对象读写偏移
 * flags   = FD_FLAG_READ | WRITE | CLOEXEC | PIPE
 *
 * 没有 type/proto 字段. write() 统一发 IO_WRITE, read() 统一发 IO_READ.
 * Pipe 是唯一例外: 使用 raw ipc_send/ipc_recv.
 */

#ifndef _XNIX_FD_H
#define _XNIX_FD_H

#include <stdint.h>
#include <xnix/abi/handle.h>

#define FD_MAX 64

/* fd 标志 */
#define FD_FLAG_READ    0x01
#define FD_FLAG_WRITE   0x02
#define FD_FLAG_CLOEXEC 0x04
#define FD_FLAG_PIPE    0x08 /* pipe: 用 raw send/recv 而非 IO 协议 */
#define FD_FLAG_DIR     0x10 /* 目录对象: after-open 控制面 */

struct fd_entry {
    handle_t handle;     /* IPC endpoint */
    uint32_t session;    /* 服务端 session ID */
    uint32_t offset;     /* 对象读写偏移 */
    uint8_t  flags;      /* FD_FLAG_* */
};

/**
 * 初始化 fd 表, 绑定 stdin/stdout/stderr
 */
void fd_table_init(void);

/**
 * 分配最小可用 fd
 * @return fd >= 0 成功, -1 无可用 fd
 */
int fd_alloc(void);

/**
 * 分配指定 fd (dup2 用)
 * @return 0 成功, -1 失败
 */
int fd_alloc_at(int fd);

/**
 * 释放 fd (不关闭内核 handle)
 */
void fd_free(int fd);

/**
 * 获取 fd 条目
 * @return fd_entry 指针, 无效时返回 NULL
 */
struct fd_entry *fd_get(int fd);

/**
 * 获取 fd 对应的内核 handle
 * @return handle 值, 无效时返回 HANDLE_INVALID
 */
handle_t fd_get_handle(int fd);

/**
 * 安装 fd 条目
 */
struct fd_entry *fd_install(int fd, handle_t handle, uint32_t session, uint32_t offset,
                            uint8_t flags);

#endif /* _XNIX_FD_H */
