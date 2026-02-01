/**
 * @file blocked.c
 * @brief 线程阻塞和唤醒机制实现
 */

#include <drivers/timer.h>

#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/sync.h>

/* 阻塞线程链表(等待唤醒) */
static struct thread *blocked_list = NULL;

/* 调度器锁 - 需要与 sched.c 共享锁来保护阻塞链表 */
extern spinlock_t sched_lock;

void sched_blocked_list_add(struct thread *t) {
    uint32_t flags = spin_lock_irqsave(&sched_lock);
    t->next        = blocked_list;
    blocked_list   = t;
    spin_unlock_irqrestore(&sched_lock, flags);
}

void sched_blocked_list_remove(struct thread *t) {
    uint32_t        flags = spin_lock_irqsave(&sched_lock);
    struct thread **pp    = &blocked_list;
    while (*pp) {
        if (*pp == t) {
            *pp     = t->next;
            t->next = NULL;
            spin_unlock_irqrestore(&sched_lock, flags);
            return;
        }
        pp = &(*pp)->next;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
}

struct thread **sched_get_blocked_list(void) {
    return &blocked_list;
}

struct thread *sched_lookup_blocked(tid_t tid) {
    uint32_t       flags = spin_lock_irqsave(&sched_lock);
    struct thread *t     = blocked_list;
    while (t) {
        if (t->tid == tid) {
            spin_unlock_irqrestore(&sched_lock, flags);
            return t;
        }
        t = t->next;
    }
    spin_unlock_irqrestore(&sched_lock, flags);
    return NULL;
}

void sched_block(void *wait_chan) {
    struct thread *current = sched_current();
    if (!current) {
        return;
    }

    struct sched_policy *current_policy = sched_get_policy();
    uint32_t             flags          = spin_lock_irqsave(&sched_lock);

    /* 检查是否有挂起的唤醒 */
    if (current->pending_wakeup) {
        current->pending_wakeup = false;
        spin_unlock_irqrestore(&sched_lock, flags);
        return; /* 不阻塞, 直接返回 */
    }

    current->state     = THREAD_BLOCKED;
    current->wait_chan = wait_chan;

    if (current_policy && current_policy->dequeue) {
        current_policy->dequeue(current);
    }

    /* 加入阻塞链表,等待唤醒 */
    current->next = blocked_list;
    blocked_list  = current;

    spin_unlock_irqrestore(&sched_lock, flags);

    /*
     * 调用 schedule() 切换到其他线程
     * schedule() 会自己管理中断状态,不需要我们额外处理
     */
    schedule();

    /* 被唤醒后从这里继续执行 */
    /* 清除 pending_wakeup 标志,防止下次 block 误判 */
    current->pending_wakeup = false;
}

void sched_wakeup(void *wait_chan) {
    struct sched_policy *current_policy = sched_get_policy();
    if (!current_policy) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);

    /* 遍历阻塞链表,唤醒所有 wait_chan 匹配的线程 */
    struct thread **pp = &blocked_list;
    while (*pp) {
        struct thread *t = *pp;
        if (t->wait_chan == wait_chan) {
            /* 从阻塞链表移除 */
            *pp          = t->next;
            t->next      = NULL;
            t->wait_chan = NULL;
            t->state     = THREAD_READY;

            /* 重新加入运行队列 */
            cpu_id_t cpu = current_policy->select_cpu ? current_policy->select_cpu(t) : 0;
            current_policy->enqueue(t, cpu);
        } else {
            pp = &(*pp)->next;
        }
    }

    spin_unlock_irqrestore(&sched_lock, flags);
}

void sched_wakeup_thread(struct thread *t) {
    struct sched_policy *current_policy = sched_get_policy();
    if (!current_policy || !t) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);

    /* 尝试从阻塞链表移除该线程 */
    struct thread **pp      = &blocked_list;
    bool            removed = false;
    while (*pp) {
        if (*pp == t) {
            *pp     = t->next;
            t->next = NULL;
            removed = true;
            break;
        }
        pp = &(*pp)->next;
    }

    t->wait_chan      = NULL;
    t->pending_wakeup = true; /* 标记有挂起的唤醒 */

    /* 只有当线程真正处于阻塞状态(成功从阻塞链表移除)时, 才将其加入运行队列 */
    /* 如果线程还在运行(RUNNING)或就绪(READY), 则不需要再次加入, 否则会导致运行队列损坏(double
     * enqueue) */
    if (removed || t->state == THREAD_BLOCKED) {
        t->state = THREAD_READY; /* 修正状态 */
        /* 重新加入运行队列 */
        cpu_id_t cpu = current_policy->select_cpu ? current_policy->select_cpu(t) : 0;
        current_policy->enqueue(t, cpu);
    }

    spin_unlock_irqrestore(&sched_lock, flags);
}

/**
 * 带超时的阻塞
 *
 * @param wait_chan  等待通道
 * @param timeout_ms 超时时间(毫秒),0 表示无限等待
 * @return true 正常唤醒,false 超时唤醒
 */
bool sched_block_timeout(void *wait_chan, uint32_t timeout_ms) {
    struct thread *current = sched_current();
    if (!current) {
        return false;
    }

    struct sched_policy *current_policy = sched_get_policy();
    uint32_t             flags          = spin_lock_irqsave(&sched_lock);

    /* 检查是否有挂起的唤醒 */
    if (current->pending_wakeup) {
        current->pending_wakeup = false;
        spin_unlock_irqrestore(&sched_lock, flags);
        return true;
    }

    current->state     = THREAD_BLOCKED;
    current->wait_chan = wait_chan;

    /* 设置超时唤醒时间 */
    if (timeout_ms > 0) {
        uint32_t ticks = (timeout_ms * CFG_SCHED_HZ + 999) / 1000;
        if (ticks == 0) {
            ticks = 1;
        }
        current->wakeup_tick = timer_get_ticks() + ticks;
    } else {
        current->wakeup_tick = 0;
    }

    if (current_policy && current_policy->dequeue) {
        current_policy->dequeue(current);
    }

    /* 加入阻塞链表 */
    current->next = blocked_list;
    blocked_list  = current;

    spin_unlock_irqrestore(&sched_lock, flags);

    schedule();

    /* 被唤醒后判断是否超时 */
    current->pending_wakeup = false;

    /* 如果 wakeup_tick 被清零,说明是超时唤醒 */
    if (timeout_ms > 0 && current->wakeup_tick == 0) {
        return false; /* 超时 */
    }

    /* 正常唤醒,清除超时设置 */
    current->wakeup_tick = 0;
    return true;
}
