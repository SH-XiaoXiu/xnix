/**
 * @file kernel/sys/sys_irq.c
 * @brief IRQ 绑定系统调用
 */

#include <ipc/notification.h>
#include <sys/syscall.h>
#include <xnix/abi/irq.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/irq.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>

/* SYS_IRQ_BIND: ebx=irq, ecx=notif_handle(-1 表示无), edx=bits */
static int32_t sys_irq_bind(const uint32_t *args) {
    uint8_t                  irq          = (uint8_t)args[0];
    handle_t                 notif_handle = (handle_t)args[1];
    uint32_t                 bits         = args[2];
    struct process          *proc         = process_current();
    struct ipc_notification *notif        = NULL;

    if (!proc) {
        return -ESRCH;
    }

    /* 检查 IRQ 绑定权限 */
    char perm_name[32];
    snprintf(perm_name, sizeof(perm_name), "xnix.irq.%u", irq);
    if (!perm_check_name(proc, perm_name)) {
        /* 如果没有特定 IRQ 权限,检查通用权限 */
        if (!perm_check_name(proc, "xnix.irq.all")) {
            return -EPERM;
        }
    }

    /* notification 可选 */
    if (notif_handle != HANDLE_INVALID) {
        /* Notification 通常不需要额外权限,只要拥有 handle */
        notif = handle_resolve(proc, notif_handle, HANDLE_NOTIFICATION, PERM_ID_INVALID);
        if (!notif) {
            return -EINVAL; /* 或 EPERM */
        }
    }

    return irq_bind_notification(irq, notif, bits);
}

/* SYS_IRQ_UNBIND: ebx=irq */
static int32_t sys_irq_unbind(const uint32_t *args) {
    uint8_t irq = (uint8_t)args[0];

    return irq_unbind_notification(irq);
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
