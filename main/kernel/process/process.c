/**
 * @file process.c
 * @brief 进程管理实现
 */

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>

#include "arch/cpu.h"

/* 全局进程链表 */
static struct process *process_list = NULL;
static spinlock_t      process_list_lock;

/* PID 分配器 */
static pid_t next_pid = 1;

/* 内核进程(PID 0) */
static struct process kernel_process;

void process_subsystem_init(void) {
    /* 初始化内核进程 */
    kernel_process.pid          = 0;
    kernel_process.name         = "kernel";
    kernel_process.state        = PROCESS_RUNNING;
    kernel_process.exit_code    = 0;
    kernel_process.page_table   = NULL;
    kernel_process.cap_table    = cap_table_create();
    kernel_process.threads      = NULL;
    kernel_process.thread_count = 0;
    kernel_process.thread_lock  = mutex_create();
    kernel_process.parent       = NULL;
    kernel_process.children     = NULL;
    kernel_process.next_sibling = NULL;
    kernel_process.next         = NULL;
    kernel_process.refcount     = 1;

    process_list = &kernel_process;

    pr_info("Process subsystem initialized (kernel PID 0)");
}

pid_t process_alloc_pid(void) {
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    pid_t pid = next_pid++;

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);
    return pid;
}

struct process *process_find_by_pid(pid_t pid) {
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    struct process *proc = process_list;
    while (proc) {
        if (proc->pid == pid) {
            process_ref(proc);
            spin_unlock(&process_list_lock);
            cpu_irq_restore(flags);
            return proc;
        }
        proc = proc->next;
    }

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);
    return NULL;
}

void process_ref(struct process *proc) {
    if (!proc) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    proc->refcount++;
    cpu_irq_restore(flags);
}

void process_unref(struct process *proc) {
    if (!proc) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    proc->refcount--;
    if (proc->refcount == 0) {
        cpu_irq_restore(flags);

        /* 销毁进程 */
        if (proc->cap_table) {
            cap_table_destroy(proc->cap_table);
        }
        if (proc->thread_lock) {
            mutex_destroy(proc->thread_lock);
        }

        /* 从进程链表移除 */
        flags = cpu_irq_save();
        spin_lock(&process_list_lock);

        struct process **pp = &process_list;
        while (*pp) {
            if (*pp == proc) {
                *pp = proc->next;
                break;
            }
            pp = &(*pp)->next;
        }

        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        kfree(proc);
        return;
    }
    cpu_irq_restore(flags);
}

process_t process_create(const char *name) {
    struct process *proc = kzalloc(sizeof(struct process));
    if (!proc) {
        return NULL;
    }

    proc->pid          = process_alloc_pid();
    proc->name         = name;
    proc->state        = PROCESS_RUNNING;
    proc->exit_code    = 0;
    proc->page_table   = NULL;
    proc->cap_table    = cap_table_create();
    proc->threads      = NULL;
    proc->thread_count = 0;
    proc->thread_lock  = mutex_create();
    proc->parent       = NULL;
    proc->children     = NULL;
    proc->next_sibling = NULL;
    proc->refcount     = 1;

    if (!proc->cap_table || !proc->thread_lock) {
        if (proc->cap_table) {
            cap_table_destroy(proc->cap_table);
        }
        if (proc->thread_lock) {
            mutex_destroy(proc->thread_lock);
        }
        kfree(proc);
        return NULL;
    }

    /* 加入进程链表 */
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    proc->next   = process_list;
    process_list = proc;

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);

    return proc;
}

void process_destroy(process_t proc) {
    if (!proc) {
        return;
    }

    /* TODO: 终止所有线程 */

    process_unref(proc);
}

void process_add_thread(struct process *proc, struct thread *t) {
    if (!proc || !t) {
        return;
    }

    mutex_lock(proc->thread_lock);

    t->next       = proc->threads;
    proc->threads = t;
    proc->thread_count++;

    mutex_unlock(proc->thread_lock);
}

void process_remove_thread(struct process *proc, struct thread *t) {
    if (!proc || !t) {
        return;
    }

    mutex_lock(proc->thread_lock);

    struct thread **pp = &proc->threads;
    while (*pp) {
        if (*pp == t) {
            *pp     = t->next;
            t->next = NULL;
            proc->thread_count--;
            break;
        }
        pp = &(*pp)->next;
    }

    mutex_unlock(proc->thread_lock);
}

struct process *process_get_current(void) {
    struct thread *t = sched_current();
    if (!t) {
        return &kernel_process;
    }

    if (!t->owner) {
        return &kernel_process;
    }

    return t->owner;
}

process_t process_current(void) {
    return process_get_current();
}

pid_t process_get_pid(process_t proc) {
    return proc ? proc->pid : PID_INVALID;
}

const char *process_get_name(process_t proc) {
    return proc ? proc->name : NULL;
}

process_state_t process_get_state(process_t proc) {
    return proc ? proc->state : PROCESS_ZOMBIE;
}

void process_init(void) {
    process_subsystem_init();
}
