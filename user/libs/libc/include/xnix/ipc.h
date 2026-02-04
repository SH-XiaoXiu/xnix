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
    void    *data;
    uint32_t size;
};

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

/* 错误码 */
#define IPC_OK          ABI_IPC_OK
#define IPC_ERR_INVALID ABI_IPC_ERR_INVALID
#define IPC_ERR_PERM    ABI_IPC_ERR_PERM
#define IPC_ERR_TIMEOUT ABI_IPC_ERR_TIMEOUT
#define IPC_ERR_CLOSED  ABI_IPC_ERR_CLOSED
#define IPC_ERR_NOMEM   ABI_IPC_ERR_NOMEM

#endif /* _XNIX_IPC_H */
