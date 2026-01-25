#ifndef KERNEL_IPC_NOTIFICATION_H
#define KERNEL_IPC_NOTIFICATION_H

#include <xnix/sync.h>
#include <xnix/types.h>

struct thread;

/**
 * Notification 对象
 *
 * 用于异步事件通知.
 * 发送者设置 pending_bits (OR 操作).
 * 接收者等待 pending_bits 非零,然后清除并返回.
 */
struct ipc_notification {
    spinlock_t     lock;
    uint32_t       pending_bits; /* 待处理事件位图 */
    struct thread *wait_queue;   /* 等待线程 (通常只有一个) */
    uint32_t       refcount;
};

/**
 * 增加引用计数
 */
void notification_ref(void *ptr);

/**
 * 减少引用计数
 * 减到 0 时释放对象
 */
void notification_unref(void *ptr);

#endif /* KERNEL_IPC_NOTIFICATION_H */
