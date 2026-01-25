/**
 * @file ipc.h
 * @brief IPC 系统 API
 */

#ifndef XNIX_IPC_H
#define XNIX_IPC_H

#include <xnix/capability.h>
#include <xnix/config.h>
#include <xnix/types.h>

/*
 * 消息结构
 */
/* 消息寄存器(短消息快速路径) */
#define IPC_MSG_REGS CFG_IPC_MSG_REGS
struct ipc_msg_regs {
    uint32_t data[IPC_MSG_REGS]; /* 8 个 32 位字 */
};

/* 消息缓冲区(长消息) */
struct ipc_msg_buffer {
    void  *data;
    size_t size;
};

/* 消息句柄(能力传递) */
#define IPC_MSG_CAPS_MAX CFG_IPC_MSG_CAPS_MAX
struct ipc_msg_caps {
    cap_handle_t handles[IPC_MSG_CAPS_MAX];
    uint32_t     count;
};

/* 完整消息 */
struct ipc_message {
    struct ipc_msg_regs   regs;   /* 短数据 */
    struct ipc_msg_buffer buffer; /* 长数据, 缓冲区(可选) */
    struct ipc_msg_caps   caps;   /* 能力句柄(可选) */
    uint32_t              flags;
};

/* 消息标志 */
#define IPC_FLAG_NO_BLOCK (1 << 0) /* 非阻塞 */
#define IPC_FLAG_TIMEOUT  (1 << 1) /* 使用超时 */

/* 错误码 */
#define IPC_OK          0
#define IPC_ERR_INVALID (-1) /* 无效句柄 */
#define IPC_ERR_PERM    (-2) /* 权限不足 */
#define IPC_ERR_TIMEOUT (-3) /* 超时 */
#define IPC_ERR_CLOSED  (-4) /* endpoint 已关闭 */
#define IPC_ERR_NOMEM   (-5) /* 内存不足 */

/* spin_unlock
 * IPC 原语
 * spin_unlock
 */

/**
 * 创建 Endpoint
 * @return Endpoint 句柄, 失败返回 CAP_HANDLE_INVALID
 */
cap_handle_t endpoint_create(void);

/**
 * 发送消息
 *
 * @param ep_handle  目标 Endpoint
 * @param msg        消息内容
 * @param timeout_ms 超时时间(ms)
 * @return 0 成功, 负数失败
 */
int ipc_send(cap_handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms);

/**
 * 接收消息
 *
 * @param ep_handle  源 Endpoint
 * @param msg        消息缓冲区
 * @param timeout_ms 超时时间(ms)
 * @return 0 成功, 负数失败
 */
int ipc_receive(cap_handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms);

/**
 * RPC 调用 (Send + Receive)
 *
 * @param ep_handle  目标 Endpoint
 * @param request    请求消息
 * @param reply      回复缓冲区
 * @param timeout_ms 超时时间(ms)
 * @return 0 成功, 负数失败
 */
int ipc_call(cap_handle_t ep_handle, struct ipc_message *request, struct ipc_message *reply,
             uint32_t timeout_ms);

/**
 * RPC 回复
 *
 * @param reply 回复消息
 * @return 0 成功, 负数失败
 */
int ipc_reply(struct ipc_message *reply);

/*
 * 异步 IPC
 */
/**
 * 异步发送(消息入队,立即返回)
 *
 * @param ep_handle 目标 Endpoint
 * @param msg       消息内容
 * @return 0 成功, 负数失败
 */
int ipc_send_async(cap_handle_t ep_handle, struct ipc_message *msg);

/**
 * 等待多个对象(Endpoint 或 Notification)
 */
#define IPC_WAIT_MAX 8
struct ipc_wait_set {
    cap_handle_t handles[IPC_WAIT_MAX];
    uint32_t     count;
};

/**
 * 返回就绪的句柄
 *
 * @param set        等待集合
 * @param timeout_ms 超时时间
 * @return 就绪的句柄,超时返回 CAP_HANDLE_INVALID
 */
cap_handle_t ipc_wait_any(struct ipc_wait_set *set, uint32_t timeout_ms);

/**
 * 创建 Notification
 *
 * @return Notification 句柄
 */
cap_handle_t notification_create(void);

/**
 * 发送信号(非阻塞,设置 bit)
 *
 * @param notif_handle Notification 句柄
 * @param bits         要设置的位
 */
void notification_signal(cap_handle_t notif_handle, uint32_t bits);

/**
 * 等待信号(阻塞直到有信号)
 *
 * @param notif_handle Notification 句柄
 * @return 收到的位
 */
uint32_t notification_wait(cap_handle_t notif_handle);

/*
 * 内核接口
 *  */

/**
 * 初始化 IPC 子系统
 */
void ipc_init(void);

#endif /* XNIX_IPC_H */
