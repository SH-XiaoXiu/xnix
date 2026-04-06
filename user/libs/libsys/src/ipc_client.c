/**
 * @file ipc_client.c
 * @brief libsys IPC client helpers
 */

#include <errno.h>
#include <string.h>
#include <xnix/sys/ipc.h>
#include <xnix/syscall.h>

int sys_ipc_call_simple(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t *result,
                        uint32_t timeout) {
    if (ep == HANDLE_INVALID) {
        errno = EINVAL;
        return -1;
    }

    struct abi_ipc_message msg   = {0};
    struct abi_ipc_message reply = {0};

    msg.regs.data[0] = opcode;
    msg.regs.data[1] = arg;

    if (sys_ipc_call(ep, (struct ipc_message *)&msg, (struct ipc_message *)&reply, timeout) < 0) {
        return -1;
    }

    if (result) {
        *result = reply.regs.data[0];
    }

    return 0;
}

int sys_ipc_send_simple(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t timeout) {
    if (ep == HANDLE_INVALID) {
        errno = EINVAL;
        return -1;
    }

    struct abi_ipc_message msg = {0};
    msg.regs.data[0] = opcode;
    msg.regs.data[1] = arg;

    if (sys_ipc_send(ep, (struct ipc_message *)&msg, timeout) < 0) {
        return -1;
    }

    return 0;
}

int sys_ipc_send_simple_noreply(handle_t ep, uint32_t opcode, uint32_t arg, uint32_t timeout) {
    return sys_ipc_send_simple(ep, opcode, arg, timeout);
}

void sys_ipc_builder_init(struct sys_ipc_builder *builder, uint32_t opcode) {
    if (!builder) {
        return;
    }

    memset(builder, 0, sizeof(*builder));
    builder->msg.regs.data[0] = opcode;
}

int sys_ipc_builder_add_arg(struct sys_ipc_builder *builder, uint32_t arg) {
    if (!builder) {
        errno = EINVAL;
        return -1;
    }
    if (builder->arg_count >= XNIX_SYS_IPC_MAX_ARGS) {
        errno = EOVERFLOW;
        return -1;
    }

    builder->msg.regs.data[builder->arg_count + 1] = arg;
    builder->arg_count++;
    return 0;
}

void sys_ipc_builder_set_buffer(struct sys_ipc_builder *builder, const void *data, uint32_t size) {
    if (!builder) {
        return;
    }

    builder->msg.buffer.data = (uint64_t)(uintptr_t)data;
    builder->msg.buffer.size = size;
}

int sys_ipc_builder_call(struct sys_ipc_builder *builder, handle_t ep,
                         struct abi_ipc_message *reply, uint32_t timeout) {
    if (!builder || !reply || ep == HANDLE_INVALID) {
        errno = EINVAL;
        return -1;
    }

    if (sys_ipc_call(ep, (struct ipc_message *)&builder->msg, (struct ipc_message *)reply, timeout)
        < 0) {
        return -1;
    }
    return 0;
}

int sys_ipc_builder_send(struct sys_ipc_builder *builder, handle_t ep, uint32_t timeout) {
    if (!builder || ep == HANDLE_INVALID) {
        errno = EINVAL;
        return -1;
    }

    if (sys_ipc_send(ep, (struct ipc_message *)&builder->msg, timeout) < 0) {
        return -1;
    }
    return 0;
}
