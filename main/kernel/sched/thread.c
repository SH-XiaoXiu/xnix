/**
 * @file thread.c
 * @brief 线程生命周期管理实现
 */

#include <arch/cpu.h>

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/sched/tid.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>

/* 待清理的已退出线程(切换后释放) */
static DEFINE_PER_CPU(struct thread *, zombie_thread);

/* Idle 线程 (Per-CPU) */
static DEFINE_PER_CPU(struct thread *, idle_thread);

/* 调度器锁 - 需要与 sched.c 共享锁 */
extern spinlock_t sched_lock;

/**
 * 清理僵尸线程
 */
void sched_cleanup_zombie(void) {
    cpu_id_t        cpu   = cpu_current_id();
    struct thread **headp = per_cpu_ptr(zombie_thread, cpu);
    struct thread **pp    = headp;

    while (*pp) {
        struct thread *z = *pp;
        if (!(z->is_detached || z->has_been_joined)) {
            pp = &z->next;
            continue;
        }

        *pp     = z->next;
        z->next = NULL;

        if (z->owner) {
            process_remove_thread(z->owner, z);
        }

        tid_free(z->tid);
        kfree(z->stack);
        kfree(z);
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

/**
 * 内部线程创建函数
 */
static struct thread *sched_spawn(const char *name, void (*entry)(void *), void *arg,
                                  struct process *owner) {
    struct sched_policy *current_policy = sched_get_policy();
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

    t->tid = tid_alloc();
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
    t->joiner_tid    = TID_INVALID;
    t->ipc_peer      = TID_INVALID;

    thread_init_stack(t, entry, arg);

    cpu_id_t cpu = current_policy->select_cpu ? current_policy->select_cpu(t) : 0;
    current_policy->enqueue(t, cpu);

    pr_info("Thread %d '%s' created", t->tid, t->name);
    return t;
}

/**
 * Idle 线程入口
 */
static void idle_task(void *arg) {
    (void)arg;
    while (1) {
        cpu_halt();
    }
}

/**
 * 初始化 idle 线程
 * 由 sched_init() 调用
 */
void thread_init_idle(void) {
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

struct thread *sched_get_idle_thread(cpu_id_t cpu) {
    return per_cpu(idle_thread, cpu);
}

struct thread **sched_get_zombie_list(cpu_id_t cpu) {
    return per_cpu_ptr(zombie_thread, cpu);
}

/*
 * 公共 API 实现
 */

thread_t thread_create(const char *name, void (*entry)(void *), void *arg) {
    return sched_spawn(name, entry, arg, NULL);
}

thread_t thread_create_with_owner(const char *name, void (*entry)(void *), void *arg,
                                  struct process *owner) {
    return sched_spawn(name, entry, arg, owner);
}

void thread_force_exit(struct thread *t) {
    uint32_t             flags          = spin_lock_irqsave(&sched_lock);
    struct sched_policy *current_policy = sched_get_policy();

    if (t->state == THREAD_EXITED) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return;
    }

    t->state       = THREAD_EXITED;
    t->exit_code   = -1;
    t->is_detached = true;

    /* 从运行队列移除 */
    if (current_policy && current_policy->dequeue) {
        current_policy->dequeue(t);
    }

    /* 从阻塞链表移除 */
    struct thread **blocked_list = sched_get_blocked_list();
    struct thread **pp           = blocked_list;
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

        struct sched_policy *current_policy = sched_get_policy();

        current->state     = THREAD_EXITED;
        current->exit_code = code;

        pr_info("Thread %d '%s' exited with code %d", current->tid, current->name, code);

        /* 检查是否是进程的最后一个线程 */
        struct process *proc = current->owner;
        if (proc && proc->pid != 0) {
            /* 检查进程是否只剩一个线程(当前线程) */
            bool last_thread = (proc->thread_count <= 1);
            if (last_thread) {
                process_exit(proc, code);
            }
        }

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

struct thread *thread_find_by_tid(tid_t tid) {
    uint32_t flags = spin_lock_irqsave(&sched_lock);

    for (cpu_id_t cpu = 0; cpu < CFG_MAX_CPUS; cpu++) {
        struct runqueue *rq = sched_get_runqueue(cpu);

        if (rq->current && rq->current->tid == tid) {
            spin_unlock_irqrestore(&sched_lock, flags);
            return rq->current;
        }

        struct thread *t = rq->head;
        while (t) {
            if (t->tid == tid) {
                spin_unlock_irqrestore(&sched_lock, flags);
                return t;
            }
            t = t->next;
        }

        t = per_cpu(idle_thread, cpu);
        if (t && t->tid == tid) {
            spin_unlock_irqrestore(&sched_lock, flags);
            return t;
        }

        t = per_cpu(zombie_thread, cpu);
        while (t) {
            if (t->tid == tid) {
                spin_unlock_irqrestore(&sched_lock, flags);
                return t;
            }
            t = t->next;
        }
    }

    struct thread *t = *sched_get_blocked_list();
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

/*
 * 线程访问器
 */

tid_t thread_get_tid(thread_t t) {
    return t ? t->tid : TID_INVALID;
}

const char *thread_get_name(thread_t t) {
    return t ? t->name : NULL;
}

thread_state_t thread_get_state(thread_t t) {
    return t ? t->state : THREAD_EXITED;
}

void thread_yield(void) {
    sched_yield();
}

thread_t thread_current(void) {
    return sched_current();
}
