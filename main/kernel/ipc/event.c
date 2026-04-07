/**
 * @file event.c
 * @brief Event 对象实现
 */

#include <arch/cpu.h>

#include <ipc/event.h>
#include <ipc/wait.h>
#include <xnix/handle.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>
#include <xnix/thread_def.h>

/*
 * Event 对象管理
 */
void event_ref(void *ptr) {
    struct ipc_event *event = ptr;
    uint32_t          flags;

    if (!event) {
        return;
    }

    flags = cpu_irq_save();
    event->refcount++;
    cpu_irq_restore(flags);
}

void event_unref(void *ptr) {
    struct ipc_event *event = ptr;
    uint32_t          flags;

    if (!event) {
        return;
    }

    flags = cpu_irq_save();
    event->refcount--;
    if (event->refcount == 0) {
        cpu_irq_restore(flags);
        kfree(event);
        return;
    }
    cpu_irq_restore(flags);
}

handle_t event_create(void) {
    struct ipc_event *event;
    handle_t          handle;
    struct process   *current;

    current = process_current();
    if (!current) {
        return HANDLE_INVALID;
    }

    event = kzalloc(sizeof(struct ipc_event));
    if (!event) {
        return HANDLE_INVALID;
    }

    spin_init(&event->lock);
    event->pending_bits = 0;
    event->wait_queue   = NULL;
    event->poll_queue   = NULL;
    event->refcount     = 1;

    /* 分配句柄: 默认给予读写和管理权限 */
    handle = handle_alloc(current, HANDLE_EVENT, event, NULL);

    if (handle == HANDLE_INVALID) {
        kfree(event);
        return HANDLE_INVALID;
    }

    return handle;
}

/*
 * Event 操作
 */

void event_signal_by_ptr(struct ipc_event *event, uint32_t bits) {
    struct thread *waiter;

    if (!event || bits == 0) {
        return;
    }

    spin_lock(&event->lock);

    /* 设置 bits */
    event->pending_bits |= bits;

    /* 唤醒 poll 等待者 (ipc_wait_any) */
    poll_wakeup(event->poll_queue);

    /* 唤醒等待者 (Broadcast: 唤醒所有等待者) */
    if (event->wait_queue) {
        /* 获取所有待处理的 bits, 并分发给所有等待者 */
        uint32_t delivery_bits = event->pending_bits;
        event->pending_bits    = 0; /* 事件已分发, 清除状态 */

        struct thread *waiter_list = event->wait_queue;
        event->wait_queue          = NULL; /* 清空等待队列 */

        spin_unlock(&event->lock);

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

    spin_unlock(&event->lock);
}

void event_signal(handle_t event_handle, uint32_t bits) {
    struct process     *proc = process_current();
    struct handle_entry entry;
    struct ipc_event   *event;

    if (!proc || bits == 0) {
        return;
    }

    if (handle_acquire(proc, event_handle, HANDLE_EVENT, &entry) < 0) {
        return;
    }
    event = entry.object;

    event_signal_by_ptr(event, bits);
    handle_object_put(entry.type, entry.object);
}

uint32_t event_wait(handle_t event_handle) {
    struct process     *proc = process_current();
    struct thread      *current;
    struct handle_entry entry;
    struct ipc_event   *event;
    uint32_t            bits;

    if (!proc) {
        return 0;
    }

    if (handle_acquire(proc, event_handle, HANDLE_EVENT, &entry) < 0) {
        return 0;
    }
    event = entry.object;

    current = sched_current();

    spin_lock(&event->lock);

    /* 检查是否有 pending bits */
    if (event->pending_bits != 0) {
        bits                = event->pending_bits;
        event->pending_bits = 0; /* 清除 bits (自动复位) */
        spin_unlock(&event->lock);
        handle_object_put(entry.type, entry.object);
        return bits;
    }

    /* 没有事件, 加入等待队列 */
    current->wait_next = event->wait_queue; /* 使用 wait_next 链接 */
    event->wait_queue  = current;

    spin_unlock(&event->lock);

    /* 阻塞等待 */
    sched_block(event);

    /* 被唤醒, 检查接收到的 bits */
    bits                   = current->notified_bits;
    current->notified_bits = 0; /* 复位 */

    handle_object_put(entry.type, entry.object);
    return bits;
}
