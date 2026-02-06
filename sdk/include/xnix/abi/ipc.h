/**
 * @file abi/ipc.h
 * @brief IPC ABI 定义
 *
 * 内核和用户态必须使用相同的定义.
 */

#ifndef XNIX_ABI_IPC_H
#define XNIX_ABI_IPC_H

#include <xnix/abi/handle.h>
#include <xnix/abi/stdint.h>
#include <xnix/abi/types.h>

/*
 * IPC 常量(不可更改)
 */

/** 消息寄存器数量(短消息快速路径) */
#define ABI_IPC_MSG_REGS 8

/** 消息中最多句柄数 */
#define ABI_IPC_MSG_HANDLES_MAX 4

/*
 * IPC 消息结构
 */

/** 消息寄存器(短消息) */
struct abi_ipc_msg_regs {
    uint32_t data[ABI_IPC_MSG_REGS];
};

/** 消息缓冲区(长消息,指向用户空间内存) */
struct abi_ipc_msg_buffer {
    uint64_t data; /* 用户空间指针,用 u64 保证跨架构兼容 */
    uint32_t size;
    uint32_t _pad;
};

/** 消息句柄传递 */
struct abi_ipc_msg_handles {
    handle_t handles[ABI_IPC_MSG_HANDLES_MAX];
    uint32_t count;
};

/** 完整 IPC 消息 */
struct abi_ipc_message {
    struct abi_ipc_msg_regs    regs;    /* 短数据 */
    struct abi_ipc_msg_buffer  buffer;  /* 长数据缓冲区(可选) */
    struct abi_ipc_msg_handles handles; /* 句柄(可选) */
    uint32_t                   flags;
};

/*
 * IPC 标志
 */
#define ABI_IPC_FLAG_NONBLOCK (1 << 0) /* 非阻塞 */
#define ABI_IPC_FLAG_TIMEOUT  (1 << 1) /* 使用超时 */

/*
 * IPC 错误码
 */
#define ABI_IPC_OK          0
#define ABI_IPC_ERR_INVALID (-1) /* 无效句柄/参数 */
#define ABI_IPC_ERR_PERM    (-2) /* 权限不足 */
#define ABI_IPC_ERR_TIMEOUT (-3) /* 超时 */
#define ABI_IPC_ERR_CLOSED  (-4) /* endpoint 已关闭 */
#define ABI_IPC_ERR_NOMEM   (-5) /* 内存不足 */

/*
 * 便捷宏:计算消息寄存器可用的 payload 字节数
 * 第一个寄存器通常用于 opcode,剩余可用于数据
 */
#define ABI_IPC_MSG_PAYLOAD_BYTES ((ABI_IPC_MSG_REGS - 1) * sizeof(uint32_t))

#endif /* XNIX_ABI_IPC_H */
