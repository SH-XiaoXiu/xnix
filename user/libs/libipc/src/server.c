/**
 * @file server.c
 * @brief IPC 服务端辅助实现
 */

#include <ipc/server.h>
#include <string.h>

int ipc_server_dispatch(const struct ipc_dispatch_entry *table, uint32_t table_size, void *ctx,
                        const struct abi_ipc_message *msg, struct abi_ipc_message *reply) {
    if (!table || !msg || !reply) {
        return IPC_ERR_INVALID;
    }

    uint32_t opcode = ipc_msg_get_opcode(msg);

    /* 查找处理函数 */
    for (uint32_t i = 0; i < table_size; i++) {
        if (table[i].opcode == opcode) {
            if (!table[i].handler) {
                return IPC_ERR_INVALID;
            }
            return table[i].handler(ctx, msg, reply);
        }
    }

    /* 未知操作码 */
    return IPC_ERR_INVALID;
}

int ipc_msg_get_buffer(const struct abi_ipc_message *msg, const void **data, uint32_t *size) {
    if (!msg || !data || !size) {
        return IPC_ERR_INVALID;
    }

    if (msg->buffer.size == 0 || msg->buffer.data == 0) {
        *data = NULL;
        *size = 0;
        return IPC_ERR_INVALID;
    }

    *data = (const void *)(uintptr_t)msg->buffer.data;
    *size = msg->buffer.size;

    return IPC_OK;
}

void ipc_reply_result(struct abi_ipc_message *reply, uint32_t result) {
    if (!reply) {
        return;
    }

    memset(reply, 0, sizeof(*reply));
    reply->regs.data[0] = result;
}

void ipc_reply_data(struct abi_ipc_message *reply, uint32_t result, const void *data,
                    uint32_t size) {
    if (!reply) {
        return;
    }

    memset(reply, 0, sizeof(*reply));
    reply->regs.data[0] = result;
    reply->buffer.data  = (uint64_t)(uintptr_t)data;
    reply->buffer.size  = size;
}
