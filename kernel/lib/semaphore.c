/**
 * @file semaphore.c
 * @brief 信号量实现
 *
 * 信号量 = 计数器 + 等待队列
 *   count > 0: 可用资源数
 *   count = 0: 无可用资源，down 操作需要等待
 *
 * 典型用途：
 *   - count=1: 二元信号量，等价于 mutex
 *   - count=N: 限制并发数（连接池、缓冲区槽位）
 */

#include <xnix/sched.h>
#include <xnix/sync.h>

void semaphore_init(semaphorephore_t *s, int count) {
    s->count   = count;
    s->waiters = NULL;
    spin_init(&s->guard);
}

void semaphore_down(semaphorephore_t *s) {
    uint32_t flags = spin_lock_irqsave(&s->guard);

    while (s->count <= 0) {
        /* 无可用资源，等待 */
        spin_unlock_irqrestore(&s->guard, flags);

        sched_block(s);

        flags = spin_lock_irqsave(&s->guard);
    }

    /* 获取一个资源 */
    s->count--;

    spin_unlock_irqrestore(&s->guard, flags);
}

void semaphore_up(semaphorephore_t *s) {
    uint32_t flags = spin_lock_irqsave(&s->guard);

    s->count++;

    spin_unlock_irqrestore(&s->guard, flags);

    /* 唤醒等待者 */
    sched_wakeup(s);
}
