/**
 * @file kernel/sys/sys_irq.c
 * @brief IRQ 绑定系统调用
 */

#include <ipc/event.h>
#include <sys/syscall.h>
#include <xnix/abi/irq.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/irq.h>
#include <xnix/cap.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>

/* SYS_IRQ_BIND: ebx=irq, ecx=event_handle(-1 表示无), edx=bits */
static int32_t sys_irq_bind(const uint32_t *args) {
    uint8_t             irq          = (uint8_t)args[0];
    handle_t            event_handle = (handle_t)args[1];
    uint32_t            bits         = args[2];
    struct process     *proc         = process_current();
    struct ipc_event   *event        = NULL;
    struct handle_entry entry;

    if (!proc) {
        return -ESRCH;
    }

    /* 检查 IRQ 绑定权限 */
    if (!cap_check_irq(proc, irq)) {
        return -EPERM;
    }

    /* event 可选 */
    if (event_handle != HANDLE_INVALID) {
        if (handle_acquire(proc, event_handle, HANDLE_EVENT, &entry) < 0) {
            return -EINVAL;
        }
        event = entry.object;
    }

    int ret = irq_bind_event(irq, proc, event, bits);
    if (event_handle != HANDLE_INVALID) {
        handle_object_put(entry.type, entry.object);
    }
    return ret;
}

/* SYS_IRQ_UNBIND: ebx=irq */
static int32_t sys_irq_unbind(const uint32_t *args) {
    uint8_t         irq  = (uint8_t)args[0];
    struct process *proc = process_current();

    if (!proc) {
        return -ESRCH;
    }

    return irq_unbind_event(irq, proc);
}

/* SYS_IRQ_READ: ebx=irq, ecx=buf, edx=size, esi=flags */
static int32_t sys_irq_read(const uint32_t *args) {
    uint8_t  irq   = (uint8_t)args[0];
    uint8_t *buf   = (uint8_t *)args[1];
    size_t   size  = (size_t)args[2];
    uint32_t flags = args[3];
    bool     block = !(flags & IRQ_READ_NONBLOCK);

    return irq_user_read(irq, buf, size, block);
}

void sys_irq_init(void) {
    syscall_register(SYS_IRQ_BIND, sys_irq_bind, 3, "irq_bind");
    syscall_register(SYS_IRQ_UNBIND, sys_irq_unbind, 1, "irq_unbind");
    syscall_register(SYS_IRQ_READ, sys_irq_read, 4, "irq_read");
}
