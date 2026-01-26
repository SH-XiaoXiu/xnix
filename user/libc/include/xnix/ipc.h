/**
 * @file ipc.h
 * @brief IPC 类型定义（用户态版本）
 */

#ifndef _XNIX_IPC_H
#define _XNIX_IPC_H

#include <stdint.h>

/* IPC 消息寄存器数量（需与内核配置一致） */
#ifndef IPC_MSG_REGS
#define IPC_MSG_REGS 8
#endif

/* 消息寄存器 */
struct ipc_msg_regs {
    uint32_t data[IPC_MSG_REGS];
};

/* 消息缓冲区 */
struct ipc_msg_buffer {
    void    *data;
    uint32_t size;
};

/* 能力句柄数组 */
#ifndef IPC_MSG_CAPS_MAX
#define IPC_MSG_CAPS_MAX 4
#endif

struct ipc_msg_caps {
    uint32_t handles[IPC_MSG_CAPS_MAX];
    uint32_t count;
};

/* 完整消息 */
struct ipc_message {
    struct ipc_msg_regs   regs;
    struct ipc_msg_buffer buffer;
    struct ipc_msg_caps   caps;
    uint32_t              flags;
};

/* 能力句柄类型 */
typedef uint32_t cap_handle_t;
#define CAP_HANDLE_INVALID 0xFFFFFFFFu

#endif /* _XNIX_IPC_H */
