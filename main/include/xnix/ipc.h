/**
 * @file ipc.h
 * @brief IPC 系统 API
 */

#ifndef XNIX_IPC_H
#define XNIX_IPC_H

#include <xnix/abi/handle.h>
#include <xnix/abi/ipc.h>
#include <xnix/types.h>

#define IPC_MSG_REGS        ABI_IPC_MSG_REGS
#define IPC_MSG_HANDLES_MAX ABI_IPC_MSG_HANDLES_MAX

/* 内核使用的消息结构(与 ABI 结构布局兼容) */
struct ipc_msg_regs {
    uint32_t data[IPC_MSG_REGS];
};

struct ipc_msg_buffer {
    void  *data;
    size_t size;
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
    tid_t                  sender_tid; /* 发送者 TID (receive 时填充, 用于延迟回复) */
};

/* 消息标志 */
#define IPC_FLAG_NO_BLOCK ABI_IPC_FLAG_NONBLOCK
#define IPC_FLAG_TIMEOUT  ABI_IPC_FLAG_TIMEOUT

/* 错误码 */
#define IPC_OK          ABI_IPC_OK
#define IPC_ERR_INVALID ABI_IPC_ERR_INVALID
#define IPC_ERR_PERM    ABI_IPC_ERR_PERM
#define IPC_ERR_TIMEOUT ABI_IPC_ERR_TIMEOUT
#define IPC_ERR_CLOSED  ABI_IPC_ERR_CLOSED
#define IPC_ERR_NOMEM   ABI_IPC_ERR_NOMEM

/*
 * IPC 原语
 */

/**
 * 创建 Endpoint
 * @return Endpoint 句柄, 失败返回 HANDLE_INVALID
 */
handle_t endpoint_create(const char *name);

/**
 * 发送消息
 *
 * @param ep_handle  目标 Endpoint
 * @param msg        消息内容
 * @param timeout_ms 超时时间(ms)
 * @return 0 成功, 负数失败
 */
int ipc_send(handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms);

/**
 * 接收消息
 *
 * @param ep_handle  源 Endpoint
 * @param msg        消息缓冲区
 * @param timeout_ms 超时时间(ms)
 * @return 0 成功, 负数失败
 */
int ipc_receive(handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms);

/**
 * RPC 调用 (Send + Receive)
 *
 * @param ep_handle  目标 Endpoint
 * @param request    请求消息
 * @param reply      回复缓冲区
 * @param timeout_ms 超时时间(ms)
 * @return 0 成功, 负数失败
 */
int ipc_call(handle_t ep_handle, struct ipc_message *request, struct ipc_message *reply,
             uint32_t timeout_ms);

/**
 * RPC 回复
 *
 * @param reply 回复消息
 * @return 0 成功, 负数失败
 */
int ipc_reply(struct ipc_message *reply);

/**
 * 延迟回复: 回复指定的发送者
 *
 * @param sender_tid 发送者的 TID (从 msg->sender_tid 获取)
 * @param reply      回复消息
 * @return 0 成功, 负数失败
 */
int ipc_reply_to(tid_t sender_tid, struct ipc_message *reply);

/**
 * 等待多个对象(Endpoint 或 Notification)
 */
#define IPC_WAIT_MAX 8
struct ipc_wait_set {
    handle_t handles[IPC_WAIT_MAX];
    uint32_t count;
};

/**
 * 返回就绪的句柄
 *
 * @param set        等待集合
 * @param timeout_ms 超时时间
 * @return 就绪的句柄,超时返回 HANDLE_INVALID
 */
handle_t ipc_wait_any(struct ipc_wait_set *set, uint32_t timeout_ms);

/**
 * 创建 Notification
 *
 * @return Notification 句柄
 */
handle_t notification_create(void);

/**
 * 发送信号(非阻塞,设置 bit)
 *
 * @param notif_handle Notification 句柄
 * @param bits         要设置的位
 */
void notification_signal(handle_t notif_handle, uint32_t bits);

/**
 * 等待信号(阻塞直到有信号)
 *
 * @param notif_handle Notification 句柄
 * @return 收到的位
 */
uint32_t notification_wait(handle_t notif_handle);

/*
 * 内核接口
 *  */

/**
 * 初始化 IPC 子系统
 */
void ipc_init(void);

#endif /* XNIX_IPC_H */
