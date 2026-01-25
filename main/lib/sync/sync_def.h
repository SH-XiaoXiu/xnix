/**
 * @file sync_def.h
 * @brief 同步原语完整定义
 *
 * 公共 API 见 <xnix/sync.h>
 */

#ifndef KERNEL_LIB_SYNC_DEF_H
#define KERNEL_LIB_SYNC_DEF_H

#include <xnix/sync.h>

struct thread; /* 前向声明 */

/**
 * Mutex 互斥锁
 */
struct mutex {
    volatile uint32_t locked;
    struct thread    *owner;   /* 持有者，用于调试和递归检测 */
    struct thread    *waiters; /* 等待队列 */
    spinlock_t        guard;   /* 保护 waiters 队列的自旋锁 因为waiters的操作也需要原子操作 */
};

/**
 * Semaphore 信号量
 */
struct semaphore {
    volatile int   count;
    struct thread *waiters;
    spinlock_t     guard;
};

/**
 * Condition Variable 条件变量
 */
struct condvar {
    struct thread *waiters;
    spinlock_t     guard;
};

#endif
