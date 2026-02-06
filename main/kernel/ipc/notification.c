/**
 * @file notification.c
 * @brief Notification 对象实现
 */

#include <arch/cpu.h>

#include <ipc/endpoint.h>
#include <ipc/notification.h>
#include <xnix/handle.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>
#include <xnix/thread_def.h>

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

handle_t notification_create(void) {
    struct ipc_notification *notif;
    handle_t                 handle;
    struct process          *current;

    current = process_current();
    if (!current) {
        return HANDLE_INVALID;
    }

    notif = kzalloc(sizeof(struct ipc_notification));
    if (!notif) {
        return HANDLE_INVALID;
    }

    spin_init(&notif->lock);
    notif->pending_bits = 0;
    notif->wait_queue   = NULL;
    notif->poll_queue   = NULL;
    notif->refcount     = 0; /* handle_alloc 会增加引用计数 */

    /* 分配句柄: 默认给予读写和管理权限 */
    handle = handle_alloc(current, HANDLE_NOTIFICATION, notif, NULL);

    if (handle == HANDLE_INVALID) {
        kfree(notif);
        return HANDLE_INVALID;
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

    /* 唤醒 poll 等待者 (ipc_wait_any) */
    struct poll_entry *pe = notif->poll_queue;
    while (pe) {
        pe->triggered = true;
        sched_wakeup_thread(pe->waiter);
        pe = pe->next;
    }

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

void notification_signal(handle_t notif_handle, uint32_t bits) {
    struct process          *proc = process_current();
    struct ipc_notification *notif;

    if (!proc || bits == 0) {
        return;
    }

    /* 查找 Notification */
    notif = handle_resolve(proc, notif_handle, HANDLE_NOTIFICATION, PERM_ID_INVALID);
    if (!notif) {
        return;
    }

    notification_signal_by_ptr(notif, bits);
}

uint32_t notification_wait(handle_t notif_handle) {
    struct process          *proc = process_current();
    struct thread           *current;
    struct ipc_notification *notif;
    uint32_t                 bits;

    if (!proc) {
        return 0;
    }

    /* 查找 Notification */
    notif = handle_resolve(proc, notif_handle, HANDLE_NOTIFICATION, PERM_ID_INVALID);
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
