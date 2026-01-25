/**
 * @file sync.h
 * @brief 同步原语
 *
 * spinlock 临界区短,不睡眠
 * mutex    临界区长,会睡眠
 * semaphore 计数型,用于资源池
 * condvar   等待条件成立
 *
 * 完整定义见 <sync/sync_def.h>
 */

#ifndef XNIX_SYNC_H
#define XNIX_SYNC_H

#include <arch/atomic.h>

#include <xnix/types.h>

/**
 * 自旋锁
 *
 * 获取不到就循环等待.临界区必须短,不能睡眠.
 */
typedef struct {
    atomic_t locked;
} spinlock_t;

#define SPINLOCK_INIT {.locked = ATOMIC_INIT(0)}

void     spin_init(spinlock_t *lock);
void     spin_lock(spinlock_t *lock);
void     spin_unlock(spinlock_t *lock);
bool     spin_trylock(spinlock_t *lock);
uint32_t spin_lock_irqsave(spinlock_t *lock);
void     spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags);

/**
 * 互斥锁
 *
 * 获取不到就睡眠让出 CPU.临界区可以长,可以有阻塞操作.
 */
typedef struct mutex mutex_t;

mutex_t *mutex_create(void);
void     mutex_destroy(mutex_t *m);
void     mutex_init(mutex_t *m);
void     mutex_lock(mutex_t *m);
void     mutex_unlock(mutex_t *m);

/**
 * 信号量
 *
 * 计数器,down 时 count--(为 0 则等待),up 时 count++.
 * count=1 就是二元信号量,count=N 可以限制并发数.
 */
typedef struct semaphore semaphore_t;

semaphore_t *semaphore_create(int count);
void         semaphore_destroy(semaphore_t *s);
void         semaphore_init(semaphore_t *s, int count);
void         semaphore_down(semaphore_t *s);
void         semaphore_up(semaphore_t *s);

/**
 * 条件变量
 *
 * 等待某个条件成立,必须配合 mutex 用.
 * wait 会原子地释放锁并睡眠,被唤醒后重新拿锁.
 */
typedef struct condvar condvar_t;

condvar_t *condvar_create(void);
void       condvar_destroy(condvar_t *c);
void       condvar_init(condvar_t *c);
void       condvar_wait(condvar_t *c, mutex_t *m);
void       condvar_signal(condvar_t *c);
void       condvar_broadcast(condvar_t *c);

#endif
