/**
 * @file ipc.h
 * @brief IPC 类型定义
 */

#ifndef _XNIX_IPC_H
#define _XNIX_IPC_H

#include <stdint.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/ipc.h>
#include <xnix/abi/types.h>

/* 从 ABI 派生的常量 */
#define IPC_MSG_REGS        ABI_IPC_MSG_REGS
#define IPC_MSG_HANDLES_MAX ABI_IPC_MSG_HANDLES_MAX

/* 消息结构(与 ABI 布局兼容) */
struct ipc_msg_regs {
    uint32_t data[IPC_MSG_REGS];
};

struct ipc_msg_buffer {
    uint64_t data; /* 用户空间指针,使用 uint64_t 保证与 ABI 布局一致 */
    uint32_t size;
    uint32_t _pad; /* 填充,保持 16 字节对齐,与 ABI 布局匹配 */
};

/* 辅助宏:设置/获取 buffer.data 指针 */
#define IPC_BUF_SET_PTR(buf, ptr)  ((buf)->data = (uint64_t)(uintptr_t)(ptr))
#define IPC_BUF_GET_PTR(buf, type) ((type)(uintptr_t)((buf)->data))

struct ipc_msg_handles {
    handle_t handles[IPC_MSG_HANDLES_MAX];
    uint32_t count;
};

struct ipc_message {
    struct ipc_msg_regs    regs;
    struct ipc_msg_buffer  buffer;
    struct ipc_msg_handles handles;
    uint32_t               flags;
    uint32_t               sender_tid; /* 发送者 TID (receive 时填充, 用于延迟回复) */
};

/*
 * 错误处理
 *
 * IPC 系统调用使用标准 errno(定义在 <errno.h>):
 * - 成功返回 0
 * - 失败返回 -1 并设置 errno:
 *   - EINVAL: 无效参数
 *   - EPERM: 权限不足
 *   - ETIMEDOUT: 超时
 *   - ESHUTDOWN: endpoint 已关闭
 *   - ENOMEM: 内存不足
 *   - ENOTCONN: endpoint 未连接
 *   - EIO: I/O 错误
 */

#endif /* _XNIX_IPC_H */
