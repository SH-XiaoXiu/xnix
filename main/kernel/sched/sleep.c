/**
 * @file sleep.c
 * @brief 线程睡眠实现
 *
 * 睡眠 = 等待时间流逝
 *   - sleep_ticks(n): 睡眠 n 个 tick
 *   - sleep_ms(ms): 睡眠 ms 毫秒
 *
 * 实现:
 *   1. 设置 wakeup_tick = 当前时间 + 睡眠时间
 *   2. 加入阻塞链表
 *   3. 每次 tick 检查是否有线程该醒来
 */

#include <arch/cpu.h>

#include <drivers/timer.h>

#include <kernel/sched/sched.h>
#include <xnix/config.h>

/**
 * 检查并唤醒睡眠到期的线程
 * 由 sched_tick() 每次 tick 调用
 */
void sleep_check_wakeup(void) {
    struct sched_policy *policy = sched_get_policy();
    if (!policy) {
        return;
    }

    uint64_t        now = timer_get_ticks();
    struct thread **pp  = sched_get_blocked_list();

    while (*pp) {
        struct thread *t = *pp;
        /* wakeup_tick != 0 表示这是睡眠线程 */
        if (t->wakeup_tick != 0 && now >= t->wakeup_tick) {
            /* 从阻塞链表移除 */
            *pp            = t->next;
            t->next        = NULL;
            t->wakeup_tick = 0;
            t->wait_chan   = NULL;

            /* 重新加入运行队列 */
            cpu_id_t cpu = policy->select_cpu ? policy->select_cpu(t) : 0;
            policy->enqueue(t, cpu);
        } else {
            pp = &(*pp)->next;
        }
    }
}

/**
 * 睡眠指定 tick 数
 */
void sleep_ticks(uint32_t ticks) {
    if (ticks == 0) {
        return;
    }

    struct thread *current = sched_current();
    if (!current) {
        return;
    }

    struct sched_policy *policy = sched_get_policy();
    if (!policy) {
        return;
    }

    /* 设置唤醒时间 */
    current->wakeup_tick = timer_get_ticks() + ticks;
    current->state       = THREAD_BLOCKED;

    /* 从运行队列移除 */
    if (policy->dequeue) {
        policy->dequeue(current);
    }

    /* 加入阻塞链表(会被 sleep_check_wakeup 处理) */
    sched_blocked_list_add(current);

    /*
     * 循环直到被唤醒.
     * 如果没有其他可运行线程,schedule() 会返回到这里,
     * 此时通过 cpu_halt() 等待下一个中断(tick)来检查唤醒条件.
     */
    while (current->state == THREAD_BLOCKED) {
        schedule();
        if (current->state == THREAD_BLOCKED) {
            cpu_halt(); /* 等待下一个 tick 中断 */
        }
    }
}

/**
 * 睡眠指定毫秒数
 */
void sleep_ms(uint32_t ms) {
    /* 转换 ms 到 ticks: ticks = ms * HZ / 1000 */
    uint32_t ticks = (ms * CFG_SCHED_HZ + 999) / 1000; /* 向上取整 */
    if (ticks == 0) {
        ticks = 1; /* 至少睡 1 tick */
    }
    sleep_ticks(ticks);
}
