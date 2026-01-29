/**
 * @file sched.c
 * @brief 调度器核心实现
 */

#include <arch/cpu.h>

#include <kernel/irq/irq.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>
#include <xnix/vmm.h>

/* 上下文切换函数(汇编实现) */
extern void context_switch(struct thread_context *old, struct thread_context *new);
extern void context_switch_first(struct thread_context *new);

/*
 * 全局变量
 **/

/* Per-CPU 运行队列 */
static DEFINE_PER_CPU(struct runqueue, runqueue);

/* 当前调度策略 */
static struct sched_policy *current_policy = NULL;

/* TID Bitmap */
static uint32_t *tid_bitmap   = NULL;
static uint32_t  tid_capacity = 0;

/* 标记是否在中断上下文 */
static volatile bool in_interrupt = false;

/* 待清理的已退出线程(切换后释放) */
static DEFINE_PER_CPU(struct thread *, zombie_thread);

/* Idle 线程 (Per-CPU) */
static DEFINE_PER_CPU(struct thread *, idle_thread);

/* 阻塞线程链表(等待唤醒) */
static struct thread *blocked_list = NULL;

/* 调度器锁,保护运行队列和阻塞链表 */
static spinlock_t sched_lock = SPINLOCK_INIT;

static void free_tid(tid_t tid) {
    if (tid == 0 || tid >= (int32_t)tid_capacity) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&sched_lock);
    tid_bitmap[tid / 32] &= ~(1UL << (tid % 32));
    spin_unlock_irqrestore(&sched_lock, flags);
}

static tid_t alloc_tid(void) {
    uint32_t flags = spin_lock_irqsave(&sched_lock);

    for (uint32_t i = 0; i < (tid_capacity + 31) / 32; i++) {
        if (tid_bitmap[i] == 0xFFFFFFFF) {
            continue;
        }

        for (int j = 0; j < 32; j++) {
            uint32_t tid = i * 32 + j;
            if (tid >= tid_capacity) {
                break;
            }

            if (!((tid_bitmap[i] >> j) & 1)) {
                tid_bitmap[i] |= (1UL << j);
                spin_unlock_irqrestore(&sched_lock, flags);
                return tid;
            }
        }
    }

    /* 扩容 */
    uint32_t  new_capacity = tid_capacity * 2;
    uint32_t *new_bitmap   = kzalloc((new_capacity + 31) / 8);
    if (!new_bitmap) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return TID_INVALID;
    }

    memcpy(new_bitmap, tid_bitmap, (tid_capacity + 31) / 8);
    kfree(tid_bitmap);
    tid_bitmap   = new_bitmap;
    tid_capacity = new_capacity;

    /* 返回新扩容部分的第一个 ID */
    /* 上一次循环结束时, tid_capacity 之前的位都已满(或在循环中被跳过),
       但为了简单和正确,我们可以直接分配 tid_capacity / 2 (即旧容量的第一个新位) */
    /* 注意: tid_capacity 肯定是 32 的倍数(初始化时保证),所以 old_cap 就是新分配区域的起始 bit */
    tid_t tid = tid_capacity / 2;
    tid_bitmap[tid / 32] |= (1UL << (tid % 32));

    spin_unlock_irqrestore(&sched_lock, flags);
    return tid;
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
    if (cpu >= CFG_MAX_CPUS) {
        return per_cpu_ptr(runqueue, 0);
    }
    return per_cpu_ptr(runqueue, cpu);
}

/*
 * 线程创建
 **/

static void sched_cleanup_zombie(void) {
    /* 取出整个链表并清空头指针 */
    struct thread *z = this_cpu_read(zombie_thread);
    this_cpu_write(zombie_thread, NULL);

    /* 释放链表上所有 zombie 线程 */
    while (z) {
        struct thread *next = z->next;

        /* 从所属进程的线程列表中移除 */
        if (z->owner) {
            process_remove_thread(z->owner, z);
        }

        free_tid(z->tid);
        kfree(z->stack);
        kfree(z);
        z = next;
    }
}

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

    cpu_irq_enable();
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

static struct thread *sched_spawn(const char *name, void (*entry)(void *), void *arg,
                                  struct process *owner) {
    if (!current_policy) {
        return NULL;
    }

    struct thread *t = kzalloc(sizeof(struct thread));
    if (!t) {
        pr_err("Failed to allocate thread");
        return NULL;
    }

    t->stack = kmalloc(CFG_THREAD_STACK_SIZE);
    if (!t->stack) {
        pr_err("Failed to allocate stack");
        kfree(t);
        return NULL;
    }

    /* 在栈底设置 canary,用于检测栈溢出 */
    *(uint32_t *)t->stack = 0xDEADBEEF;

    t->tid = alloc_tid();
    if (t->tid == TID_INVALID) {
        pr_err("Failed to allocate TID");
        kfree(t->stack);
        kfree(t);
        return NULL;
    }

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
    t->owner         = owner;

    thread_init_stack(t, entry, arg);

    cpu_id_t cpu = current_policy->select_cpu ? current_policy->select_cpu(t) : 0;
    current_policy->enqueue(t, cpu);

    pr_info("Thread %d '%s' created", t->tid, t->name);
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
    struct thread *next = current_policy->pick_next();

    if (!next) {
        /* 如果运行队列为空, 切换到 idle 线程 */
        struct thread *idle = per_cpu(idle_thread, cpu);
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
        if (prev != per_cpu(idle_thread, cpu)) {
            if (prev->state == THREAD_RUNNING) {
                prev->state = THREAD_READY;
            }
            prev->running_on = CPU_ID_INVALID;
        }
    }

    /* 如果下一个线程是 idle 线程, 保持其状态为 READY (或者是特殊的 IDLE 状态) */
    if (next != per_cpu(idle_thread, cpu)) {
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
 * 公共 API 实现
 **/

void sched_init(void) {
    /* 分配 TID Bitmap (初始容量使用配置值) */
    tid_capacity       = (CFG_INITIAL_THREADS + 31) & ~31; /* 32 对齐 */
    size_t bitmap_size = (tid_capacity / 8);
    tid_bitmap         = kzalloc(bitmap_size);
    if (!tid_bitmap) {
        panic("Failed to allocate TID bitmap");
    }

    /* 初始化 TID Bitmap (保留 TID 0) */
    tid_bitmap[0] |= 1;

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

    /* 创建 idle 线程 (为每个 CPU 创建一个) */
    /* 注意: idle 线程不加入运行队列, 也不需要策略 */
    for (int i = 0; i < CFG_MAX_CPUS; i++) {
        struct thread *idle = kzalloc(sizeof(struct thread));
        if (idle) {
            idle->tid = 0; /* TID 0 保留给 idle (所有 idle 线程共享 TID 0) */
            /* 为了调试方便, 也可以给不同的 TID, 但 TID 0 通常是特殊的 */

            idle->name          = "idle";
            idle->state         = THREAD_READY;
            idle->priority      = 255;
            idle->stack_size    = CFG_THREAD_STACK_SIZE;
            idle->stack         = kmalloc(CFG_THREAD_STACK_SIZE);
            idle->cpus_workable = (1 << i); /* 绑定到特定 CPU */
            idle->running_on    = CPU_ID_INVALID;

            if (idle->stack) {
                /* 在栈底设置 canary */
                *(uint32_t *)idle->stack = 0xDEADBEEF;
                thread_init_stack(idle, idle_task, NULL);
                per_cpu(idle_thread, i) = idle;
            } else {
                panic("Failed to allocate idle stack for CPU%d", i);
            }
        } else {
            panic("Failed to allocate idle thread for CPU%d", i);
        }
    }
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
    /* 清除 pending_wakeup 标志,防止下次 block 误判 */
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

            /* 架构特定的线程切换 */
            arch_thread_switch(first);

            irq_eoi(0);
            context_switch_first(&first->ctx);
            /* 不会返回 */
        }
        in_interrupt = false;
        irq_eoi(0);
        return;
    }

    /* 特殊处理 idle 线程 */
    if (current == this_cpu_read(idle_thread)) {
        /* 检查是否有可运行线程 */
        if (current_policy) {
            struct thread *next = current_policy->pick_next();
            if (next) {
                /* 有可运行线程,切换过去 */
                irq_eoi(0);
                schedule();
                in_interrupt = false;
                return;
            }
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

void sched_migrate(struct thread *t, cpu_id_t target_cpu) {
    /* TODO: 多核实现 */
    (void)t;
    (void)target_cpu;
}

/*
 * 线程 API 实现
 */
thread_t thread_create(const char *name, void (*entry)(void *), void *arg) {
    return sched_spawn(name, entry, arg, NULL);
}

/* 内部版本,允许指定 owner */
thread_t thread_create_with_owner(const char *name, void (*entry)(void *), void *arg,
                                  struct process *owner) {
    return sched_spawn(name, entry, arg, owner);
}

void thread_force_exit(struct thread *t) {
    uint32_t flags = spin_lock_irqsave(&sched_lock);

    if (t->state == THREAD_EXITED) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return;
    }

    t->state     = THREAD_EXITED;
    t->exit_code = -1;

    /* 从运行队列移除 */
    if (current_policy && current_policy->dequeue) {
        current_policy->dequeue(t);
    }

    /* 从阻塞链表移除 */
    struct thread **pp = &blocked_list;
    while (*pp) {
        if (*pp == t) {
            *pp = t->next;
            break;
        }
        pp = &(*pp)->next;
    }

    /* 加入僵尸链表 */
    cpu_id_t cpu                = cpu_current_id();
    t->next                     = per_cpu(zombie_thread, cpu);
    per_cpu(zombie_thread, cpu) = t;

    spin_unlock_irqrestore(&sched_lock, flags);
}

void thread_exit(int code) {
    /* 关中断,防止在挂链过程中发生调度 */
    cpu_irq_disable();

    struct thread *current = sched_current();
    if (current) {
        /* 安全检查:绝不允许 idle 线程退出 */
        if (current == this_cpu_read(idle_thread)) {
            panic("Idle thread tried to exit!");
        }

        current->state     = THREAD_EXITED;
        current->exit_code = code;

        pr_info("Thread %d '%s' exited with code %d", current->tid, current->name, code);

        /* 从运行队列移除 */
        if (current_policy && current_policy->dequeue) {
            current_policy->dequeue(current);
        }

        /* 挂入 per-cpu 僵尸链表 */
        current->next = this_cpu_read(zombie_thread);
        this_cpu_write(zombie_thread, current);
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
