/**
 * @file notification.c
 * @brief Notification 对象实现
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/ipc/notification.h>
#include <kernel/sched/sched.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>

/*
 * Notification 对象管理
 */
void notification_ref(void *ptr) {
    struct ipc_notification *notif = ptr;
    uint32_t                 flags;

    if (!notif) {
        return;
    }

    flags = cpu_irq_save();
    notif->refcount++;
    cpu_irq_restore(flags);
}

void notification_unref(void *ptr) {
    struct ipc_notification *notif = ptr;
    uint32_t                 flags;

    if (!notif) {
        return;
    }

    flags = cpu_irq_save();
    notif->refcount--;
    if (notif->refcount == 0) {
        cpu_irq_restore(flags);
        kfree(notif);
        return;
    }
    cpu_irq_restore(flags);
}

cap_handle_t notification_create(void) {
    struct ipc_notification *notif;
    cap_handle_t             handle;
    struct process          *current;

    current = process_current();
    if (!current) {
        return CAP_HANDLE_INVALID;
    }

    notif = kzalloc(sizeof(struct ipc_notification));
    if (!notif) {
        return CAP_HANDLE_INVALID;
    }

    spin_init(&notif->lock);
    notif->pending_bits = 0;
    notif->wait_queue   = NULL;
    notif->refcount     = 0; /* cap_alloc 会增加引用计数 */

    /* 分配句柄: 默认给予读写和管理权限 */
    cap_rights_t rights = CAP_READ | CAP_WRITE | CAP_GRANT | CAP_MANAGE;
    handle              = cap_alloc(current, CAP_TYPE_NOTIFICATION, notif, rights);

    if (handle == CAP_HANDLE_INVALID) {
        kfree(notif);
        return CAP_HANDLE_INVALID;
    }

    return handle;
}

/*
 * Notification 操作
 *  */

void notification_signal_by_ptr(struct ipc_notification *notif, uint32_t bits) {
    struct thread *waiter;

    if (!notif || bits == 0) {
        return;
    }

    spin_lock(&notif->lock);

    /* 设置 bits */
    notif->pending_bits |= bits;

    /* 唤醒等待者 (Broadcast: 唤醒所有等待者) */
    if (notif->wait_queue) {
        /* 获取所有待处理的 bits, 并分发给所有等待者 */
        uint32_t delivery_bits = notif->pending_bits;
        notif->pending_bits    = 0; /* 事件已分发, 清除状态 */

        struct thread *waiter_list = notif->wait_queue;
        notif->wait_queue          = NULL; /* 清空等待队列 */

        spin_unlock(&notif->lock);

        /* 遍历链表唤醒所有线程 */
        while (waiter_list) {
            waiter            = waiter_list;
            waiter_list       = waiter->wait_next;
            waiter->wait_next = NULL;

            /* 将事件 bits 传递给线程 */
            waiter->notified_bits = delivery_bits;
            sched_wakeup_thread(waiter);
        }
        return;
    }

    spin_unlock(&notif->lock);
}

void notification_signal(cap_handle_t notif_handle, uint32_t bits) {
    struct process          *proc = process_current();
    struct ipc_notification *notif;

    if (!proc || bits == 0) {
        return;
    }

    /* 查找 Notification (需要 WRITE 权限) */
    notif = cap_lookup(proc, notif_handle, CAP_TYPE_NOTIFICATION, CAP_WRITE);
    if (!notif) {
        return;
    }

    notification_signal_by_ptr(notif, bits);
}

uint32_t notification_wait(cap_handle_t notif_handle) {
    struct process          *proc = process_current();
    struct thread           *current;
    struct ipc_notification *notif;
    uint32_t                 bits;

    if (!proc) {
        return 0;
    }

    /* 查找 Notification (需要 READ 权限) */
    notif = cap_lookup(proc, notif_handle, CAP_TYPE_NOTIFICATION, CAP_READ);
    if (!notif) {
        return 0;
    }

    current = sched_current();

    spin_lock(&notif->lock);

    /* 检查是否有 pending bits */
    if (notif->pending_bits != 0) {
        bits                = notif->pending_bits;
        notif->pending_bits = 0; /* 清除 bits (自动复位) */
        spin_unlock(&notif->lock);
        return bits;
    }

    /* 没有事件, 加入等待队列 */
    current->wait_next = notif->wait_queue; /* 使用 wait_next 链接 */
    notif->wait_queue  = current;

    spin_unlock(&notif->lock);

    /* 阻塞等待 */
    /* 使用 notif 作为 wait_chan */
    sched_block(notif);

    /* 被唤醒, 检查接收到的 bits */
    /* 此时 bits 应该已经存储在 current->notified_bits 中 (由 signal 设置) */
    /* 注意: 我们不需要再抢锁检查 notif->pending_bits, 因为那是给后续事件的 */

    bits                   = current->notified_bits;
    current->notified_bits = 0; /* 复位 */

    return bits;
}
