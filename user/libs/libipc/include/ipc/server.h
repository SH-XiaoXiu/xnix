/**
 * @file server.h
 * @brief IPC 服务端辅助 API
 *
 * 提供消息分发,参数解析,响应构造等辅助功能
 */

#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include <ipc/common.h>

/**
 * 消息处理函数类型
 *
 * @param ctx   服务上下文(可选)
 * @param msg   请求消息
 * @param reply 回复消息
 * @return 0 成功,负数失败
 */
typedef int (*ipc_handler_t)(void *ctx, const struct abi_ipc_message *msg,
                             struct abi_ipc_message *reply);

/**
 * 消息分发表项
 */
struct ipc_dispatch_entry {
    uint32_t      opcode;  /* 操作码 */
    ipc_handler_t handler; /* 处理函数 */
};

/**
 * 分发消息到处理函数
 *
 * @param table       分发表
 * @param table_size  分发表大小
 * @param ctx         服务上下文(传递给 handler)
 * @param msg         请求消息
 * @param reply       回复消息
 * @return 0 成功,负数失败
 */
int ipc_server_dispatch(const struct ipc_dispatch_entry *table, uint32_t table_size, void *ctx,
                        const struct abi_ipc_message *msg, struct abi_ipc_message *reply);

/**
 * 参数解析
 */

/**
 * 获取消息中的操作码
 *
 * @param msg 消息
 * @return 操作码
 */
static inline uint32_t ipc_msg_get_opcode(const struct abi_ipc_message *msg) {
    return msg->regs.data[0];
}

/**
 * 获取消息中的参数
 *
 * @param msg   消息
 * @param index 参数索引(0-6)
 * @return 参数值
 */
static inline uint32_t ipc_msg_get_arg(const struct abi_ipc_message *msg, uint32_t index) {
    if (index >= IPC_MAX_ARGS) {
        return 0;
    }
    return msg->regs.data[index + 1];
}

/**
 * 获取消息中的缓冲区
 *
 * @param msg  消息
 * @param data 输出:数据指针
 * @param size 输出:数据大小
 * @return 0 成功,负数失败
 */
int ipc_msg_get_buffer(const struct abi_ipc_message *msg, const void **data, uint32_t *size);

/**
 * 响应构造
 */

/**
 * 构造简单结果回复
 *
 * @param reply  回复消息
 * @param result 结果值
 */
void ipc_reply_result(struct abi_ipc_message *reply, uint32_t result);

/**
 * 构造数据回复
 *
 * @param reply  回复消息
 * @param result 结果值
 * @param data   数据指针
 * @param size   数据大小
 */
void ipc_reply_data(struct abi_ipc_message *reply, uint32_t result, const void *data,
                    uint32_t size);

/**
 * 构造错误回复
 *
 * @param reply 回复消息
 * @param error 错误码
 */
static inline void ipc_reply_error(struct abi_ipc_message *reply, int error) {
    ipc_reply_result(reply, (uint32_t)error);
}

#endif /* IPC_SERVER_H */
