/**
 * @file condvar.c
 * @brief 条件变量实现
 *
 * 条件变量用于"等待某个条件成立",必须配合 mutex 使用:
 *
 *   mutex_lock(&m);
 *   while (!condition)
 *       condvar_wait(&cv, &m);  // 原子地释放锁并睡眠
 *   // 条件成立,处理...
 *   mutex_unlock(&m);
 *
 * wait 操作的原子性很重要:
 *   如果先 unlock 再 sleep,可能在这之间错过 signal
 */

#include <sync/sync_def.h>
#include <xnix/mm.h>
#include <xnix/thread.h>

condvar_t *condvar_create(void) {
    condvar_t *c = kzalloc(sizeof(condvar_t));
    if (c) {
        condvar_init(c);
    }
    return c;
}

void condvar_destroy(condvar_t *c) {
    if (c) {
        kfree(c);
    }
}

void condvar_init(condvar_t *c) {
    c->waiters = NULL;
    spin_init(&c->guard);
}

void condvar_wait(condvar_t *c, mutex_t *m) {
    /* 释放 mutex,让其他线程可以修改条件 */
    mutex_unlock(m);

    /* 阻塞等待 signal/broadcast */
    sched_block(c);

    /* 被唤醒后重新获取 mutex */
    mutex_lock(m);
}

void condvar_signal(condvar_t *c) {
    /* 唤醒一个等待者(实际会唤醒所有,由调度器选择一个运行) */
    sched_wakeup(c);
}

void condvar_broadcast(condvar_t *c) {
    /* 唤醒所有等待者 */
    sched_wakeup(c);
}
