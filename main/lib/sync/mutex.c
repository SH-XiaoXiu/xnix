/**
 * @file mutex.c
 * @brief 互斥锁实现
 *
 * mutex = spinlock + 睡眠等待
 * 获取不到锁时让出 CPU,避免忙等浪费
 *
 * 实现要点:
 *   1. 用 spinlock 保护内部状态(locked,waiters)
 *   2. 获取失败时加入等待队列,然后释放 spinlock 并睡眠
 *   3. 释放时唤醒一个等待者
 */

#include <sync/sync_def.h>
#include <xnix/mm.h>
#include <xnix/thread.h>

mutex_t *mutex_create(void) {
    mutex_t *m = kzalloc(sizeof(mutex_t));
    if (m) {
        mutex_init(m);
    }
    return m;
}

void mutex_destroy(mutex_t *m) {
    if (m) {
        kfree(m);
    }
}

void mutex_init(mutex_t *m) {
    m->locked  = 0;
    m->owner   = NULL;
    m->waiters = NULL;
    spin_init(&m->guard);
}

void mutex_lock(mutex_t *m) {
    uint32_t flags = spin_lock_irqsave(&m->guard);

    while (m->locked) {
        /* 锁被占用,需要睡眠等待 */
        spin_unlock_irqrestore(&m->guard, flags);

        /* 阻塞当前线程,wait_chan 设为 mutex 地址 */
        sched_block(m);

        /* 被唤醒后重新获取 guard 继续检查 */
        flags = spin_lock_irqsave(&m->guard);
    }

    /* 获取锁成功 */
    m->locked = 1;
    m->owner  = thread_current();

    spin_unlock_irqrestore(&m->guard, flags);
}

void mutex_unlock(mutex_t *m) {
    uint32_t flags = spin_lock_irqsave(&m->guard);

    m->locked = 0;
    m->owner  = NULL;

    spin_unlock_irqrestore(&m->guard, flags);

    /* 唤醒所有等待此 mutex 的线程 */
    sched_wakeup(m);
}
