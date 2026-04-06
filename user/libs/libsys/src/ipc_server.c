/**
 * @file ipc_server.c
 * @brief libsys IPC server helpers
 */

#include <errno.h>
#include <string.h>
#include <xnix/sys/ipc.h>

int sys_ipc_server_dispatch(const struct sys_ipc_dispatch_entry *table, uint32_t table_size,
                            void *ctx, const struct abi_ipc_message *msg,
                            struct abi_ipc_message *reply) {
    if (!table || !msg || !reply) {
        errno = EINVAL;
        return -1;
    }

    uint32_t opcode = msg->regs.data[0];
    for (uint32_t i = 0; i < table_size; i++) {
        if (table[i].opcode == opcode) {
            if (!table[i].handler) {
                errno = EINVAL;
                return -1;
            }
            return table[i].handler(ctx, msg, reply);
        }
    }

    errno = EINVAL;
    return -1;
}

int sys_ipc_msg_get_buffer(const struct abi_ipc_message *msg, const void **data, uint32_t *size) {
    if (!msg || !data || !size) {
        errno = EINVAL;
        return -1;
    }
    if (msg->buffer.size == 0 || msg->buffer.data == 0) {
        *data = NULL;
        *size = 0;
        errno = ENOENT;
        return -1;
    }

    *data = (const void *)(uintptr_t)msg->buffer.data;
    *size = msg->buffer.size;
    return 0;
}

void sys_ipc_reply_result(struct abi_ipc_message *reply, uint32_t result) {
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
    reply->regs.data[0] = result;
}

void sys_ipc_reply_data(struct abi_ipc_message *reply, uint32_t result, const void *data,
                        uint32_t size) {
    if (!reply) {
        return;
    }
    memset(reply, 0, sizeof(*reply));
    reply->regs.data[0] = result;
    reply->buffer.data = (uint64_t)(uintptr_t)data;
    reply->buffer.size = size;
}
