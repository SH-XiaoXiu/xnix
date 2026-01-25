/**
 * @file sched.c
 * @brief 调度器核心实现
 */

#include <arch/cpu.h>

#include <drivers/irqchip.h>

#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>

/* 上下文切换函数(汇编实现) */
extern void context_switch(struct thread_context *old, struct thread_context *new);
extern void context_switch_first(struct thread_context *new);

/*
 * 全局变量
 **/

/* Per-CPU 运行队列(当前单核,数组大小为 1) */
static struct runqueue runqueues[1];

/* 当前调度策略 */
static struct sched_policy *current_policy = NULL;

/* TID 分配器 */
static tid_t next_tid = 1;

/* 标记是否在中断上下文 */
static volatile bool in_interrupt = false;

/* 待清理的已退出线程(切换后释放) */
static struct thread *zombie_thread = NULL;

/* Idle 线程 */
static struct thread *idle_thread = NULL;

/* 阻塞线程链表(等待唤醒) */
static struct thread *blocked_list = NULL;

/* 调度器锁,保护运行队列和阻塞链表 */
static spinlock_t sched_lock = SPINLOCK_INIT;

static tid_t alloc_tid(void) {
    // TODO 原子保护 回收
    return next_tid++;
}

/* 阻塞链表操作(导出给 sleep.c 使用) */

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

struct sched_policy *sched_get_policy(void) {
    return current_policy;
}

struct runqueue *sched_get_runqueue(cpu_id_t cpu) {
    (void)cpu; /* 单核忽略参数 */
    return &runqueues[0];
}

/*
 * 线程创建
 **/

/**
 * 线程入口包装器
 * 从寄存器读取 entry 和 arg(ebx=entry, esi=arg)
 */
static void thread_entry_wrapper(void) {
    void (*entry)(void *);
    void *arg;

    /* 从寄存器读取参数 */
    __asm__ volatile("mov %%ebx, %0" : "=r"(entry));
    __asm__ volatile("mov %%esi, %0" : "=r"(arg));

    cpu_irq_enable(); /* 开中断 */
    entry(arg);
    thread_exit(0);
}

/**
 * 初始化线程栈
 * 使用 callee-saved 寄存器传参:ebx=entry, esi=arg
 */
static void thread_init_stack(struct thread *t, void (*entry)(void *), void *arg) {
    uint32_t *stack_top = (uint32_t *)((char *)t->stack + t->stack_size);

    /* 压入返回地址:thread_entry_wrapper */
    *(--stack_top) = (uint32_t)thread_entry_wrapper;

    /* 设置 esp 和寄存器 */
    t->ctx.esp = (uint32_t)stack_top;
    t->ctx.ebp = 0;
    t->ctx.ebx = (uint32_t)entry; /* 传递 entry */
    t->ctx.esi = (uint32_t)arg;   /* 传递 arg */
    t->ctx.edi = 0;
}

static struct thread *sched_spawn(const char *name, void (*entry)(void *), void *arg) {
    if (!current_policy) {
        return NULL;
    }

    struct thread *t = kzalloc(sizeof(struct thread));
    if (!t) {
        kprintf("ERROR: Failed to allocate thread\n");
        return NULL;
    }

    t->stack = kmalloc(CFG_THREAD_STACK_SIZE);
    if (!t->stack) {
        kprintf("ERROR: Failed to allocate stack\n");
        kfree(t);
        return NULL;
    }

    t->tid           = alloc_tid();
    t->name          = name;
    t->state         = THREAD_READY;
    t->priority      = 0;
    t->time_slice    = 0;
    t->cpus_workable = CPUS_ALL;
    t->running_on    = CPU_ID_INVALID;
    t->policy        = NULL;
    t->stack_size    = CFG_THREAD_STACK_SIZE;
    t->next          = NULL;
    t->wait_chan     = NULL;
    t->exit_code     = 0;

    thread_init_stack(t, entry, arg);

    cpu_id_t cpu = current_policy->select_cpu ? current_policy->select_cpu(t) : 0;
    current_policy->enqueue(t, cpu);

    kprintf("Thread %d '%s' created\n", t->tid, t->name);
    return t;
}

/*
 * Idle 线程
 * 永远在运行队列之外(特殊处理),用于 CPU 空闲时运行
 */
static void idle_task(void *arg) {
    (void)arg;
    while (1) {
        cpu_halt();
    }
}

/*
 * 核心调度函数
 **/

void schedule(void) {
    if (!current_policy) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);

    /* 清理上次退出的线程 */
    if (zombie_thread) {
        kfree(zombie_thread->stack);
        kfree(zombie_thread);
        zombie_thread = NULL;
    }

    cpu_id_t         cpu  = cpu_current_id();
    struct runqueue *rq   = sched_get_runqueue(cpu);
    struct thread   *prev = rq->current;
    struct thread   *next = current_policy->pick_next();

    if (!next) {
        /* 如果运行队列为空, 切换到 idle 线程 */
        if (!idle_thread) {
            /* 尚未初始化 idle 线程, 这是一个严重错误, 但我们尝试死循环等待 */
            kprintf("ERROR: idle_thread not initialized!\n");
            spin_unlock_irqrestore(&sched_lock, flags);
            while (1) {
                cpu_halt();
            }
        }
        next = idle_thread;
    }

    if (next == prev) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return; /* 仍然是当前线程 */
    }

    /* 更新状态 */
    if (prev) {
        /* 如果上一个线程是 idle 线程, 不要修改它的状态 (它永远是 READY) */
        if (prev != idle_thread) {
            if (prev->state == THREAD_RUNNING) {
                prev->state = THREAD_READY;
            }
            prev->running_on = CPU_ID_INVALID;
        }
    }

    /* 如果下一个线程是 idle 线程, 保持其状态为 READY (或者是特殊的 IDLE 状态) */
    if (next != idle_thread) {
        next->state      = THREAD_RUNNING;
        next->running_on = cpu;
    }
    rq->current = next;

    /*
     * 上下文切换
     * 必须在切换前释放锁
     * context_switch_first 不会返回
     * 新线程从 thread_entry_wrapper 开始,不会回到这里
     * 只有被切换出去过的线程才会从 context_switch 返回
     */
    spin_unlock(&sched_lock);

    if (prev) {
        context_switch(&prev->ctx, &next->ctx);
    } else {
        context_switch_first(&next->ctx);
    }

    /* 恢复中断状态(只有从 context_switch 返回的线程会到这里) */
    cpu_irq_restore(flags);
}

/*
 * 公共 API 实现
 **/

void sched_init(void) {
    /* 初始化运行队列 */
    for (int i = 0; i < 1; i++) {
        runqueues[i].head       = NULL;
        runqueues[i].tail       = NULL;
        runqueues[i].current    = NULL;
        runqueues[i].nr_running = 0;
    }

    sched_set_policy(&sched_policy_rr);
    kprintf("Scheduler initialized\n");

    /* 创建 idle 线程 */
    /* 注意: idle 线程不加入运行队列, 也不需要策略 */
    idle_thread = kzalloc(sizeof(struct thread));
    if (idle_thread) {
        idle_thread->tid           = 0; /* TID 0 保留给 idle */
        idle_thread->name          = "idle";
        idle_thread->state         = THREAD_READY;
        idle_thread->priority      = 255;
        idle_thread->stack_size    = CFG_THREAD_STACK_SIZE;
        idle_thread->stack         = kmalloc(CFG_THREAD_STACK_SIZE);
        idle_thread->cpus_workable = CPUS_ALL;
        idle_thread->running_on    = CPU_ID_INVALID;

        if (idle_thread->stack) {
            thread_init_stack(idle_thread, idle_task, NULL);
        } else {
            kprintf("ERROR: Failed to allocate idle stack\n");
        }
    } else {
        kprintf("ERROR: Failed to allocate idle thread\n");
    }
}

void sched_set_policy(struct sched_policy *policy) {
    if (policy && policy->init) {
        policy->init();
    }
    current_policy = policy;
    if (policy) {
        kprintf("Sched policy: %s\n", policy->name);
    }
}

struct thread *sched_current(void) {
    cpu_id_t         cpu = cpu_current_id();
    struct runqueue *rq  = sched_get_runqueue(cpu);
    return rq->current;
}

void sched_yield(void) {
    schedule();
}

void sched_block(void *wait_chan) {
    struct thread *current = sched_current();
    if (!current) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);

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
    /* 清除 pending_wakeup 标志，防止下次 block 误判 */
    current->pending_wakeup = false;
}

void sched_wakeup(void *wait_chan) {
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

void sched_destroy_current(void) {
    struct thread *t = sched_current();
    if (t) {
        zombie_thread = t;
    }
}

void sched_tick(void) {
    struct thread *current = sched_current();

    in_interrupt = true; /* 标记进入中断 */

    /* 检查睡眠线程(sleep.c) */
    sleep_check_wakeup();

    /* 首次启动:没有 current 但有就绪线程 */
    if (!current && current_policy) {
        struct thread *first = current_policy->pick_next();
        if (first) {
            cpu_id_t         cpu = cpu_current_id();
            struct runqueue *rq  = sched_get_runqueue(cpu);

            first->state      = THREAD_RUNNING;
            first->running_on = cpu;
            rq->current       = first;

            irq_eoi(0);
            context_switch_first(&first->ctx);
            /* 不会返回 */
        }
        in_interrupt = false;
        irq_eoi(0);
        return;
    }

    /* 特殊处理 idle 线程 */
    if (current == idle_thread) {
        /* 检查是否有可运行线程 */
        if (current_policy) {
            struct thread *next = current_policy->pick_next();
            if (next) {
                /* 有可运行线程，切换过去 */
                irq_eoi(0);
                schedule();
                in_interrupt = false;
                return;
            }
        }
        /* 没有可运行线程，idle 继续运行 */
        in_interrupt = false;
        irq_eoi(0);
        return;
    }

    /* 普通线程处理 */
    if (!current || !current_policy || !current_policy->tick) {
        in_interrupt = false;
        irq_eoi(0);
        return;
    }

    /* 调用策略的 tick 函数 */
    bool need_resched = current_policy->tick(current);
    if (need_resched) {
        irq_eoi(0); /* 切换前发送 EOI */
        schedule();
    } else {
        irq_eoi(0);
    }

    in_interrupt = false;
}

void sched_migrate(struct thread *t, cpu_id_t target_cpu) {
    /* TODO: 多核实现 */
    (void)t;
    (void)target_cpu;
}

/*
 * 线程 API 实现(原 thread.c)
 **/

thread_t thread_create(const char *name, void (*entry)(void *), void *arg) {
    return sched_spawn(name, entry, arg);
}

void thread_exit(int code) {
    struct thread *current = sched_current();
    if (current) {
        current->state     = THREAD_EXITED;
        current->exit_code = code;
        kprintf("Thread %d '%s' exited with code %d\n", current->tid, current->name, code);
        sched_destroy_current();
    }
    schedule();
    /* 不应该返回 */
    while (1) {
        cpu_halt();
    }
}

void thread_yield(void) {
    sched_yield();
}

thread_t thread_current(void) {
    return sched_current();
}

/*
 * 线程访问器
 **/

tid_t thread_get_tid(thread_t t) {
    return t ? t->tid : TID_INVALID;
}

const char *thread_get_name(thread_t t) {
    return t ? t->name : NULL;
}

thread_state_t thread_get_state(thread_t t) {
    return t ? t->state : THREAD_EXITED;
}
