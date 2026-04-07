#ifndef KERNEL_IPC_EVENT_H
#define KERNEL_IPC_EVENT_H

#include <xnix/sync.h>
#include <xnix/types.h>

struct thread;
struct poll_entry;

/**
 * Event 对象
 *
 * 用于异步事件通知.
 * 发送者设置 pending_bits (OR 操作).
 * 接收者等待 pending_bits 非零,然后清除并返回.
 */
struct ipc_event {
    spinlock_t     lock;
    uint32_t       pending_bits; /* 待处理事件位图 */
    struct thread *wait_queue;   /* 等待线程 (通常只有一个) */
    uint32_t       refcount;

    /* Poll 等待队列(用于 ipc_wait_any) */
    struct poll_entry *poll_queue;
};

/**
 * 增加引用计数
 */
void event_ref(void *ptr);

/**
 * 减少引用计数
 * 减到 0 时释放对象
 */
void event_unref(void *ptr);

/**
 * 通过指针发送信号
 *
 * 用于 IRQ 处理器等没有进程上下文的场景.
 * 调用者需确保 event 指针有效.
 *
 * @param event event 对象指针
 * @param bits  要设置的位
 */
void event_signal_by_ptr(struct ipc_event *event, uint32_t bits);

#endif /* KERNEL_IPC_EVENT_H */
