#ifndef KERNEL_IPC_WAIT_H
#define KERNEL_IPC_WAIT_H

#include <xnix/abi/handle.h>
#include <xnix/sync.h>
#include <xnix/types.h>

struct thread;

/**
 * Poll 等待项
 *
 * 用于 ipc_wait_any 实现多路复用.
 * 一个线程可以创建多个 poll_entry 加入不同对象的 poll_queue.
 */
struct poll_entry {
    struct thread     *waiter;    /* 等待的线程 */
    handle_t           handle;    /* 对应的句柄(用于返回) */
    struct poll_entry *next;      /* 对象内的 poll 链表 */
    volatile bool      triggered; /* 是否已触发 */
};

/**
 * 通用 poll 唤醒: 唤醒 poll_queue 中所有等待者
 * 调用者必须持有对象的 lock
 */
static inline void poll_wakeup(struct poll_entry *queue) {
    struct poll_entry *pe = queue;
    while (pe) {
        pe->triggered = true;
        extern void sched_wakeup_thread(struct thread *t);
        sched_wakeup_thread(pe->waiter);
        pe = pe->next;
    }
}

/**
 * 通用 poll 移除: 从 poll_queue 中移除指定 entry
 * 调用者必须持有对象的 lock
 */
static inline void poll_remove(struct poll_entry **queue, struct poll_entry *entry) {
    struct poll_entry **pp = queue;
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

#endif /* KERNEL_IPC_WAIT_H */
