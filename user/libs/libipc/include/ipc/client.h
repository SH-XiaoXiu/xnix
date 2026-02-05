/**
 * @file client.h
 * @brief IPC 客户端辅助 API
 *
 * 提供简化的 IPC 调用接口,包括 Endpoint 缓存,Builder 模式等
 */

#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include <ipc/common.h>

/**
 * Endpoint 管理
 */

/**
 * 查找并缓存 Endpoint
 *
 * 如果之前查找过,直接返回缓存的 handle
 *
 * @param name 服务名称
 * @return Endpoint handle,失败返回 HANDLE_INVALID
 */
handle_t ipc_ep_find(const char *name);

/**
 * 清除 Endpoint 缓存
 *
 * 用于服务重启后强制重新查找
 *
 * @param name 服务名称,NULL 表示清除所有
 */
void ipc_ep_clear_cache(const char *name);

/**
 * 简化调用
 */

/**
 * 简单的单参数 RPC 调用
 *
 * @param ep      Endpoint handle
 * @param opcode  操作码
 * @param arg     参数
 * @param result  返回值(可选,NULL 表示不关心)
 * @param timeout 超时(毫秒,0 表示无限等待)
 * @return 0 成功,负数失败
 */
int ipc_call_simple(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t *result, uint32_t timeout);

/**
 * 简单的单参数单向消息
 *
 * @param ep      Endpoint handle
 * @param opcode  操作码
 * @param arg     参数
 * @param timeout 超时(毫秒,0 表示无限等待)
 * @return 0 成功,负数失败
 */
int ipc_send_simple(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t timeout);

/**
 * Builder 模式(栈上分配)
 */

/**
 * IPC Builder 结构体(栈上分配)
 */
struct ipc_builder {
    struct abi_ipc_message msg;       /* 消息结构 */
    uint32_t               arg_count; /* 已添加的参数数量 */
};

/**
 * 初始化 Builder
 *
 * @param builder Builder 指针
 * @param opcode  操作码
 */
void ipc_builder_init(struct ipc_builder *builder, uint32_t opcode);

/**
 * 添加参数
 *
 * @param builder Builder 指针
 * @param arg     参数值
 * @return 0 成功,负数失败(参数过多)
 */
int ipc_builder_add_arg(struct ipc_builder *builder, uint32_t arg);

/**
 * 设置缓冲区
 *
 * @param builder Builder 指针
 * @param data    数据指针
 * @param size    数据大小
 */
void ipc_builder_set_buffer(struct ipc_builder *builder, const void *data, uint32_t size);

/**
 * 执行 RPC 调用
 *
 * @param builder Builder 指针
 * @param ep      Endpoint handle
 * @param reply   回复消息(可选,NULL 表示单向消息)
 * @param timeout 超时(毫秒,0 表示无限等待)
 * @return 0 成功,负数失败
 */
int ipc_builder_call(struct ipc_builder *builder, handle_t ep, struct abi_ipc_message *reply,
                     uint32_t timeout);

/**
 * 发送单向消息
 *
 * @param builder Builder 指针
 * @param ep      Endpoint handle
 * @param timeout 超时(毫秒,0 表示无限等待)
 * @return 0 成功,负数失败
 */
int ipc_builder_send(struct ipc_builder *builder, handle_t ep, uint32_t timeout);

#endif /* IPC_CLIENT_H */
