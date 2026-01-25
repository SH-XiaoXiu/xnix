/**
 * @file sched.c
 * @brief 调度器核心实现
 */

#include <kernel/sched/sched.h>

#include <arch/cpu.h>

#include <drivers/irqchip.h>

#include <xnix/config.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* 上下文切换函数（汇编实现） */
extern void context_switch(struct thread_context *old, struct thread_context *new);
extern void context_switch_first(struct thread_context *new);

/*
 * 全局变量
 **/

/* Per-CPU 运行队列（当前单核，数组大小为 1） */
static struct runqueue runqueues[1];

/* 当前调度策略 */
static struct sched_policy *current_policy = NULL;

/* TID 分配器 */
static tid_t next_tid = 1;

/* 标记是否在中断上下文 */
static bool in_interrupt = false;

/* 待清理的已退出线程（切换后释放） */
static struct thread *zombie_thread = NULL;

/* 阻塞线程链表（等待唤醒） */
static struct thread *blocked_list = NULL;

static tid_t alloc_tid(void) {
    return next_tid++;
}

/* 阻塞链表操作（导出给 sleep.c 使用） */

void sched_blocked_list_add(struct thread *t) {
    t->next      = blocked_list;
    blocked_list = t;
}

void sched_blocked_list_remove(struct thread *t) {
    struct thread **pp = &blocked_list;
    while (*pp) {
        if (*pp == t) {
            *pp     = t->next;
            t->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

struct thread **sched_get_blocked_list(void) {
    return &blocked_list;
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
 * 从寄存器读取 entry 和 arg（ebx=entry, esi=arg）
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
 * 使用 callee-saved 寄存器传参：ebx=entry, esi=arg
 */
static void thread_init_stack(struct thread *t, void (*entry)(void *), void *arg) {
    uint32_t *stack_top = (uint32_t *)((char *)t->stack + t->stack_size);

    /* 压入返回地址：thread_entry_wrapper */
    *(--stack_top) = (uint32_t)thread_entry_wrapper;

    /* 设置 esp 和寄存器 */
    t->ctx.esp = (uint32_t)stack_top;
    t->ctx.ebp = 0;
    t->ctx.ebx = (uint32_t)entry;  /* 传递 entry */
    t->ctx.esi = (uint32_t)arg;    /* 传递 arg */
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
 * 核心调度函数
 **/

void schedule(void) {
    if (!current_policy) {
        return;
    }

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
        return; /* 无可运行线程 */
    }

    if (next == prev) {
        return; /* 仍然是当前线程 */
    }

    /* 更新状态 */
    if (prev) {
        if (prev->state == THREAD_RUNNING) {
            prev->state = THREAD_READY;
        }
        prev->running_on = CPU_ID_INVALID;
    }

    next->state      = THREAD_RUNNING;
    next->running_on = cpu;
    rq->current      = next;

    /* 如果在中断中，需要先发送 EOI */
    if (in_interrupt) {
        irq_eoi(0); /* PIT 中断是 IRQ 0 */
    }

    /* 上下文切换 */
    if (prev) {
        context_switch(&prev->ctx, &next->ctx);
    } else {
        context_switch_first(&next->ctx);
    }
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

    current->state     = THREAD_BLOCKED;
    current->wait_chan = wait_chan;

    if (current_policy && current_policy->dequeue) {
        current_policy->dequeue(current);
    }

    /* 加入阻塞链表，等待唤醒 */
    sched_blocked_list_add(current);

    schedule();
}

void sched_wakeup(void *wait_chan) {
    if (!current_policy) {
        return;
    }

    /* 遍历阻塞链表，唤醒所有 wait_chan 匹配的线程 */
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

    /* 检查睡眠线程（sleep.c） */
    sleep_check_wakeup();

    /* 首次启动：没有 current 但有就绪线程 */
    if (!current && current_policy) {
        struct thread *first = current_policy->pick_next();
        if (first) {
            cpu_id_t cpu = cpu_current_id();
            struct runqueue *rq = sched_get_runqueue(cpu);

            first->state = THREAD_RUNNING;
            first->running_on = cpu;
            rq->current = first;

            irq_eoi(0); /* 先发 EOI */
            context_switch_first(&first->ctx);
            /* 不会返回 */
        }
        in_interrupt = false;
        return;
    }

    if (!current || !current_policy || !current_policy->tick) {
        in_interrupt = false;
        return;
    }

    bool need_resched = current_policy->tick(current);
    if (need_resched) {
        schedule(); /* 会在内部发送 EOI */
    }

    in_interrupt = false;
}

void sched_migrate(struct thread *t, cpu_id_t target_cpu) {
    /* TODO: 多核实现 */
    (void)t;
    (void)target_cpu;
}

/*
 * 线程 API 实现（原 thread.c）
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
