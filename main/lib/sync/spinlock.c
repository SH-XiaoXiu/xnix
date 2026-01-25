/**
 * @file spinlock.c
 * @brief 自旋锁实现
 *
 * 单核:关中断即可
 * 多核:关中断 + 原子操作
 *
 * 当前实现是多核安全的,单核也能用(只是多了原子操作开销)
 */

#include <arch/atomic.h>
#include <arch/cpu.h>

#include <xnix/sync.h>

void spin_init(spinlock_t *lock) {
    atomic_set(&lock->locked, 0);
}

/*
 * spin_lock - 获取自旋锁
 *
 * 用 atomic_xchg 尝试将 locked 设为 1:
 *   - 返回 0:之前未锁定,获取成功
 *   - 返回 1:之前已锁定,继续自旋
 *
 * pause 指令:告诉 CPU 正在自旋等待,降低功耗
 */
void spin_lock(spinlock_t *lock) {
    while (atomic_xchg(&lock->locked, 1) != 0) {
        cpu_pause();
    }
}

void spin_unlock(spinlock_t *lock) {
    atomic_store_release(&lock->locked, 0);
}

/*
 * spin_trylock - 尝试获取锁
 * 返回 true 表示成功获取,false 表示锁已被占用
 */
bool spin_trylock(spinlock_t *lock) {
    return atomic_xchg(&lock->locked, 1) == 0;
}

/*
 * spin_lock_irqsave - 关中断 + 获取锁
 *   关中断后,中断处理程序不会抢占当前代码
 * 返回保存的 flags,用于之后恢复中断状态
 */
uint32_t spin_lock_irqsave(spinlock_t *lock) {
    uint32_t flags = cpu_irq_save(); /* 保存并关中断 */
    spin_lock(lock);
    return flags;
}

void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags) {
    spin_unlock(lock);
    cpu_irq_restore(flags); /* 恢复之前的中断状态 */
}
