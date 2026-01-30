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

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/config.h>

/**
 * 检查并唤醒睡眠到期的线程
 *
 * 只在 BSP (CPU 0) 上执行
 * tick 计数是全局的,由 BSP 维护
 * blocked_list 是全局的,需要避免多核竞争
 */
void sleep_check_wakeup(void) {
    /* 只在 BSP 上检查睡眠唤醒 */
    if (cpu_current_id() != 0) {
        return;
    }

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
            t->state       = THREAD_READY; /* 设置状态为就绪 */

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
 *
 * 唤醒方式:
 *   定时器到期:sleep_check_wakeup() 设置 READY 并 enqueue
 *   信号:sched_wakeup_thread() 设置 READY 并 enqueue
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
    current->wait_chan   = current; /* 设置 wait_chan 以便信号能唤醒 */
    current->state       = THREAD_BLOCKED;

    /* 从运行队列移除 */
    if (policy->dequeue) {
        policy->dequeue(current);
    }

    /* 加入阻塞链表 */
    sched_blocked_list_add(current);

    /* 切换到其他线程,唤醒后返回 */
    schedule();

    /* 清理唤醒时间 */
    current->wakeup_tick = 0;
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
