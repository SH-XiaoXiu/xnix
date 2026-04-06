/**
 * @file ipc.h
 * @brief libsys IPC helper API
 */

#ifndef XNIX_SYS_IPC_H
#define XNIX_SYS_IPC_H

#include <xnix/abi/handle.h>
#include <xnix/abi/ipc.h>

#define XNIX_SYS_IPC_MAX_ARGS 7

handle_t sys_ipc_ep_find(const char *name);
void     sys_ipc_ep_clear_cache(const char *name);

int sys_ipc_call_simple(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t *result,
                        uint32_t timeout);
int sys_ipc_send_simple(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t timeout);
int sys_ipc_send_simple_noreply(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t timeout);

struct sys_ipc_builder {
    struct abi_ipc_message msg;
    uint32_t               arg_count;
};

void sys_ipc_builder_init(struct sys_ipc_builder *builder, uint32_t opcode);
int  sys_ipc_builder_add_arg(struct sys_ipc_builder *builder, uint32_t arg);
void sys_ipc_builder_set_buffer(struct sys_ipc_builder *builder, const void *data, uint32_t size);
int  sys_ipc_builder_call(struct sys_ipc_builder *builder, handle_t ep,
                          struct abi_ipc_message *reply, uint32_t timeout);
int  sys_ipc_builder_send(struct sys_ipc_builder *builder, handle_t ep, uint32_t timeout);

typedef int (*sys_ipc_handler_t)(void *ctx, const struct abi_ipc_message *msg,
                                 struct abi_ipc_message *reply);

struct sys_ipc_dispatch_entry {
    uint32_t          opcode;
    sys_ipc_handler_t handler;
};

int sys_ipc_server_dispatch(const struct sys_ipc_dispatch_entry *table, uint32_t table_size,
                            void *ctx, const struct abi_ipc_message *msg,
                            struct abi_ipc_message *reply);
int  sys_ipc_msg_get_buffer(const struct abi_ipc_message *msg, const void **data, uint32_t *size);
void sys_ipc_reply_result(struct abi_ipc_message *reply, uint32_t result);
void sys_ipc_reply_data(struct abi_ipc_message *reply, uint32_t result, const void *data,
                        uint32_t size);

#endif /* XNIX_SYS_IPC_H */
