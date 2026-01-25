/**
 * @file process.c
 * @brief 进程管理实现
 */

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

#include "arch/cpu.h"

/* 全局进程链表 */
static struct process *process_list = NULL;
static spinlock_t      process_list_lock;

/* PID Bitmap */
static uint32_t *pid_bitmap   = NULL;
static uint32_t  pid_capacity = 0;

/* 内核进程(PID 0) */
static struct process kernel_process;

void process_subsystem_init(void) {
    /* 分配 PID Bitmap */
    pid_capacity       = (CFG_INITIAL_PROCESSES + 31) & ~31;
    size_t bitmap_size = (pid_capacity / 8);
    pid_bitmap         = kzalloc(bitmap_size);
    if (!pid_bitmap) {
        panic("Failed to allocate PID bitmap");
    }

    /* 初始化内核进程 */
    kernel_process.pid = 0;
    /* 占用 PID 0 */
    pid_bitmap[0] |= 1;

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

static void free_pid(pid_t pid) {
    if (pid == 0 || pid >= pid_capacity) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);
    pid_bitmap[pid / 32] &= ~(1UL << (pid % 32));
    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);
}

pid_t process_alloc_pid(void) {
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    for (uint32_t i = 0; i < (pid_capacity + 31) / 32; i++) {
        if (pid_bitmap[i] == 0xFFFFFFFF) {
            continue;
        }

        for (int j = 0; j < 32; j++) {
            uint32_t pid = i * 32 + j;
            if (pid >= pid_capacity) {
                break;
            }

            if (!((pid_bitmap[i] >> j) & 1)) {
                pid_bitmap[i] |= (1UL << j);
                spin_unlock(&process_list_lock);
                cpu_irq_restore(flags);
                return pid;
            }
        }
    }

    /* 扩容 */
    uint32_t  new_capacity = pid_capacity * 2;
    uint32_t *new_bitmap   = kzalloc((new_capacity + 31) / 8);
    if (!new_bitmap) {
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);
        return PID_INVALID;
    }

    memcpy(new_bitmap, pid_bitmap, (pid_capacity + 31) / 8);
    kfree(pid_bitmap);
    pid_bitmap   = new_bitmap;
    pid_capacity = new_capacity;

    /* 返回新扩容部分的第一个 ID */
    pid_t pid = pid_capacity / 2;
    pid_bitmap[pid / 32] |= (1UL << (pid % 32));

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

        free_pid(proc->pid);
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

    proc->pid = process_alloc_pid();
    if (proc->pid == PID_INVALID) {
        kfree(proc);
        return NULL;
    }

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
