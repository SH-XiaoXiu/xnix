/**
 * @file sched.c
 * @brief 调度器核心实现
 */

#include <arch/cpu.h>
#include <arch/smp.h>

#include <asm/irq.h>
#include <kernel/irq/irq.h>
#include <kernel/sched/sched.h>
#include <kernel/sched/tid.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>

/* 上下文切换函数(汇编实现) */
extern void context_switch(struct thread_context *old, struct thread_context *new);
extern void context_switch_first(struct thread_context *new);

/*
 * 全局变量
 */

/* Per-CPU 运行队列 */
static DEFINE_PER_CPU(struct runqueue, runqueue);

/* 当前调度策略 */
static struct sched_policy *current_policy = NULL;

/* 标记是否在中断上下文 */
static volatile bool in_interrupt = false;

/* 调度器锁,保护运行队列和阻塞链表 */
spinlock_t sched_lock = SPINLOCK_INIT;

/* 前向声明 */
static void balance_load(void);

/*
 * 公共 API 实现
 */

struct sched_policy *sched_get_policy(void) {
    return current_policy;
}

struct runqueue *sched_get_runqueue(cpu_id_t cpu) {
    if (cpu >= CFG_MAX_CPUS) {
        return per_cpu_ptr(runqueue, 0);
    }
    return per_cpu_ptr(runqueue, cpu);
}

/*
 * 核心调度函数
 */

void schedule(void) {
    if (!current_policy) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);

    cpu_id_t         cpu  = cpu_current_id();
    struct runqueue *rq   = sched_get_runqueue(cpu);
    struct thread   *prev = rq->current;

    /* 检查当前线程的栈 canary */
    if (prev && prev->stack && *(uint32_t *)prev->stack != 0xDEADBEEF) {
        spin_unlock(&sched_lock);
        cpu_irq_restore(flags);
        panic("Stack overflow detected! Thread '%s' (tid=%d) canary corrupted",
              prev->name ? prev->name : "?", prev->tid);
    }

    /* 处理挂起的迁移请求 */
    if (prev && prev->migrate_pending) {
        prev->migrate_pending = false;
        cpu_id_t target       = prev->migrate_target;
        prev->migrate_target  = CPU_ID_INVALID;

        /* 将线程从当前队列移到目标 CPU 队列 */
        current_policy->dequeue(prev);
        current_policy->enqueue(prev, target);

        /* 通知目标 CPU 进行调度 */
        if (target != cpu && cpu_is_online(target)) {
            smp_send_ipi(target, IPI_VECTOR_RESCHED);
        }
    }

    struct thread *next = current_policy->pick_next();

    if (!next) {
        /* 如果运行队列为空, 切换到 idle 线程 */
        struct thread *idle = sched_get_idle_thread(cpu);
        if (!idle) {
            /* 尚未初始化 idle 线程, 这是一个严重错误 */
            panic("idle_thread for CPU%u not initialized!", cpu);
        }
        next = idle;
    }

    if (next == prev) {
        spin_unlock(&sched_lock);
        sched_cleanup_zombie();
        cpu_irq_restore(flags);
        return;
    }

    /* 更新状态 */
    if (prev) {
        /* 如果上一个线程是 idle 线程, 不要修改它的状态 (它永远是 READY) */
        struct thread *idle = sched_get_idle_thread(cpu);
        if (prev != idle) {
            if (prev->state == THREAD_RUNNING) {
                prev->state = THREAD_READY;
            }
            prev->running_on = CPU_ID_INVALID;
        }
    }

    /* 如果下一个线程是 idle 线程, 保持其状态为 READY (或者是特殊的 IDLE 状态) */
    struct thread *idle = sched_get_idle_thread(cpu);
    if (next != idle) {
        next->state      = THREAD_RUNNING;
        next->running_on = cpu;
    }
    rq->current = next;

    /* 架构特定的线程切换 (VMM, TSS 等) */
    arch_thread_switch(next);

    /*
     * 上下文切换
     * 必须在切换前释放锁
     * context_switch_first 不会返回
     * 新线程从 thread_entry_wrapper 开始,不会回到这里
     * 只有被切换出去过的线程才会从 context_switch 返回
     */
    spin_unlock(&sched_lock);

    if (prev) {
        /* 切换后在新线程栈上运行,此时清理 zombie 是安全的 */
        context_switch(&prev->ctx, &next->ctx);

        /*
         * context_switch 返回后,当前在恢复的线程上下文中运行.
         * 必须重新设置 TSS ESP0 为当前线程的内核栈,否则下次从用户态
         * 进入内核态时会使用错误的栈.
         */
        struct thread *current = sched_current();
        if (current && current->stack) {
            extern void tss_set_stack(uint32_t ss0, uint32_t esp0);
            uint32_t    esp0 = (uint32_t)current->stack + current->stack_size;
            tss_set_stack(0x10, esp0); /* KERNEL_DS = 0x10 */
        }

        sched_cleanup_zombie();
    } else {
        /* context_switch_first 不返回,zombie 会在下次 context_switch 返回时清理 */
        context_switch_first(&next->ctx);
        __builtin_unreachable();
    }

    cpu_irq_restore(flags);
}

/*
 * 初始化
 */

void sched_init(void) {
    /* 初始化 TID 管理 */
    tid_init();

    /* 初始化运行队列 */
    for (int i = 0; i < CFG_MAX_CPUS; i++) {
        struct runqueue *rq = per_cpu_ptr(runqueue, i);
        rq->head            = NULL;
        rq->tail            = NULL;
        rq->current         = NULL;
        rq->nr_running      = 0;
    }

    sched_set_policy(&sched_policy_rr);
    pr_info("Scheduler initialized");

    /* 初始化 idle 线程 */
    thread_init_idle();

    /* 注册 THREAD 能力类型(已废弃,改为 Handle) */
    /* cap_register_type(CAP_TYPE_THREAD, ...); */
}

void sched_set_policy(struct sched_policy *policy) {
    if (policy && policy->init) {
        policy->init();
    }
    current_policy = policy;
    if (policy) {
        pr_info("Sched policy: %s", policy->name);
    }
}

struct thread *sched_current(void) {
    cpu_id_t         cpu = cpu_current_id();
    struct runqueue *rq  = sched_get_runqueue(cpu);
    return rq->current;
}

void sched_yield(void) {
    struct thread *current = sched_current();
    if (current && current_policy) {
        current->time_slice = 0;
    }
    schedule();
}

void sched_tick(void) {
    struct thread *current = sched_current();

    in_interrupt = true; /* 标记进入中断 */

    /* 更新全局 tick 计数 */
    sched_stat_tick();

    /* 检查睡眠线程(sleep.c) */
    sleep_check_wakeup();

    /* 周期性负载均衡 */
    balance_load();

    /* 首次启动:没有 current 但有就绪线程 */
    if (!current && current_policy) {
        uint32_t         flags = spin_lock_irqsave(&sched_lock);
        cpu_id_t         cpu   = cpu_current_id();
        struct runqueue *rq    = sched_get_runqueue(cpu);
        struct thread   *first = current_policy->pick_next();
        if (first) {
            first->state      = THREAD_RUNNING;
            first->running_on = cpu;
            rq->current       = first;

            /* 架构特定的线程切换 */
            arch_thread_switch(first);

            /*
             * 先发送 EOI,再释放锁,最后切换.
             * context_switch_first 不返回,新线程会自己启用中断.
             */
            irq_eoi(0);
            spin_unlock(&sched_lock);
            context_switch_first(&first->ctx);
            /* 不会返回 */
        }
        spin_unlock_irqrestore(&sched_lock, flags);
        in_interrupt = false;
        irq_eoi(0);
        return;
    }

    /* 特殊处理 idle 线程 */
    cpu_id_t       cpu  = cpu_current_id();
    struct thread *idle = sched_get_idle_thread(cpu);
    if (current == idle) {
        /* 统计 idle 时间 */
        sched_stat_idle_tick();
        idle->cpu_ticks++;

        /* 检查是否有可运行线程(只读检查,不需要锁) */
        struct runqueue *rq = sched_get_runqueue(cpu);
        if (rq->nr_running > 0) {
            /* 有可运行线程,切换过去 */
            irq_eoi(0);
            schedule();
            in_interrupt = false;
            return;
        }
        /* 没有可运行线程,idle 继续运行 */
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

int sched_migrate(struct thread *t, cpu_id_t target_cpu) {
    /* 参数验证 */
    if (!t) {
        return -EINVAL;
    }

    if (target_cpu >= percpu_cpu_count() || !cpu_is_online(target_cpu)) {
        return -EINVAL;
    }

    /* 检查 CPU 亲和性 */
    if (!CPUS_TEST(t->cpus_workable, target_cpu)) {
        return -EPERM;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);

    /* 根据线程状态处理 */
    switch (t->state) {
    case THREAD_READY:
        /* READY 线程:直接迁移 */
        current_policy->dequeue(t);
        current_policy->enqueue(t, target_cpu);

        /* 通知目标 CPU */
        if (target_cpu != cpu_current_id() && cpu_is_online(target_cpu)) {
            spin_unlock_irqrestore(&sched_lock, flags);
            smp_send_ipi(target_cpu, IPI_VECTOR_RESCHED);
            return 0;
        }
        break;

    case THREAD_RUNNING:
        /* RUNNING 线程:设置迁移标志,由 schedule() 处理 */
        t->migrate_pending = true;
        t->migrate_target  = target_cpu;

        /* 发送 IPI 到线程当前运行的 CPU,触发调度 */
        cpu_id_t source_cpu = t->running_on;
        if (source_cpu != CPU_ID_INVALID && source_cpu != cpu_current_id()) {
            spin_unlock_irqrestore(&sched_lock, flags);
            smp_send_ipi(source_cpu, IPI_VECTOR_RESCHED);
            return 0;
        }
        break;

    case THREAD_BLOCKED:
        /* BLOCKED 线程:唤醒时自然会选择 CPU,不需要迁移 */
        spin_unlock_irqrestore(&sched_lock, flags);
        return -EBUSY;

    default:
        spin_unlock_irqrestore(&sched_lock, flags);
        return -EINVAL;
    }

    spin_unlock_irqrestore(&sched_lock, flags);
    return 0;
}

/*
 * 周期性负载均衡
 */

#define BALANCE_INTERVAL 100 /* 每 100 tick 执行一次 (约 1 秒) */

static void balance_load(void) {
    static uint32_t counter = 0;

    /* 仅在 CPU 0 执行,避免多核竞争 */
    if (cpu_current_id() != 0) {
        return;
    }

    if (++counter < BALANCE_INTERVAL) {
        return;
    }
    counter = 0;

    uint32_t total_cpus = percpu_cpu_count();
    if (total_cpus <= 1) {
        return;
    }

    /* 找到最繁忙和最空闲的 CPU */
    cpu_id_t busiest  = 0;
    cpu_id_t idlest   = 0;
    uint32_t max_load = 0;
    uint32_t min_load = ~0u;

    for (cpu_id_t i = 0; i < total_cpus; i++) {
        if (!cpu_is_online(i)) {
            continue;
        }
        struct runqueue *rq = sched_get_runqueue(i);
        if (rq->nr_running > max_load) {
            max_load = rq->nr_running;
            busiest  = i;
        }
        if (rq->nr_running < min_load) {
            min_load = rq->nr_running;
            idlest   = i;
        }
    }

    /* 负载差异超过阈值时迁移(差异 > 2 表示不均衡) */
    if (max_load > min_load + 2) {
        struct runqueue *rq = sched_get_runqueue(busiest);

        /* 从队尾取(最近加入的,cache 局部性差) */
        struct thread *t = rq->tail;
        if (t && t != rq->current && CPUS_TEST(t->cpus_workable, idlest)) {
            sched_migrate(t, idlest);
        }
    }
}
