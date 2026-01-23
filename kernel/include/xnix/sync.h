/**
 * @file sync.h
 * @brief 同步原语
 *
 * 多线程访问共享资源需要同步，否则会出现竞态条件 (race condition)
 * 原语从底层到高层：atomic → spinlock → mutex/semaphorephore
 */

#ifndef XNIX_SYNC_H
#define XNIX_SYNC_H

#include <xnix/types.h>

/*
 * Spinlock 自旋锁
 *
 * 最简单的锁：获取不到就原地循环等待（自旋）
 *
 * 适用：临界区很短（几条指令），不值得睡眠切换的开销
 * 注意：持有时不能睡眠，否则死锁
 *
 * 单核实现：关中断就行
 * 多核实现：关中断 + 原子操作（防止其他 CPU 竞争）这个待实现
 */
typedef struct {
    volatile uint32_t locked; /* 0=未锁定, 1=已锁定 */
} spinlock_t;

#define SPINLOCK_INIT {.locked = 0}

void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);    /* 获取锁（自旋） */
void spin_unlock(spinlock_t *lock);  /* 释放锁 */
bool spin_trylock(spinlock_t *lock); /* 尝试获取，失败立即返回 false */

/* 带中断保存的版本 - 防止中断处理程序中的死锁 */
uint32_t spin_lock_irqsave(spinlock_t *lock);
void     spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags);

/*
 * Mutex 互斥锁
 *
 * 获取不到就睡眠, 让出 CPU
 * 适用于临界区较长 或者可能阻塞的操作
 * 其实最底层原子的锁是自旋
 * 互斥锁的部分实现的操作甚至需要自旋锁的保护
 * 主要是操作内存资源的时候需要 然后构建出互斥锁之后 就可以往上构建更抽象的应用层面的锁
 * 他们理论上是挂起 不占用资源 也不会涉及到操作内核调度本身的内存资源
 * 所以理论上只要触及到调度器本身的资源的操作 就需要最小的同步原语 后续上层构建的线程
 * 或者其他的这种资源 用互斥锁就行
 */

typedef struct {
    volatile uint32_t locked;
    struct thread    *owner;   /* 持有者，用于调试和递归检测 */
    struct thread    *waiters; /* 等待队列 */
    spinlock_t        guard;   /* 保护 waiters 队列的自旋锁 因为waiters的操作也需要原子操作 */
} mutex_t;

#define MUTEX_INIT {.locked = 0, .owner = NULL, .waiters = NULL, .guard = SPINLOCK_INIT}

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);   /* 获取锁（可能睡眠） */
void mutex_unlock(mutex_t *m); /* 释放锁 */

/*
 * semaphore 信号量
 *
 * 计数器：down 减一（为 0 则等待），up 加一（唤醒等待者）
 *
 * count=1 → 二元信号量，等价于 mutex
 * count=N → 允许 N 个线程同时进入（如：连接池、缓冲区槽位）
 */
typedef struct {
    volatile int   count;
    struct thread *waiters;
    spinlock_t     guard;
} semaphorephore_t;

void semaphore_init(semaphorephore_t *s, int count);
void semaphore_down(semaphorephore_t *s); /* P 操作：count--，为 0 则等待 */
void semaphore_up(semaphorephore_t *s);   /* V 操作：count++，唤醒等待者 */

/*
 * Condition Variable 条件变量
 *
 * "等待某个条件成立"的原语，必须配合 mutex 使用：
 *   mutex_lock(&m);
 *   while (!condition)
 *       condvar_wait(&cv, &m);  // 释放锁 + 睡眠，被唤醒后重新获取锁
 *   // 条件成立，处理...
 *   mutex_unlock(&m);
 */
typedef struct {
    struct thread *waiters;
    spinlock_t     guard;
} condvar_t;

void condvar_init(condvar_t *c);
void condvar_wait(condvar_t *c, mutex_t *m); /* 释放 m + 睡眠，唤醒后重新获取 m */
void condvar_signal(condvar_t *c);           /* 唤醒一个等待者 */
void condvar_broadcast(condvar_t *c);        /* 唤醒所有等待者 */

#endif
