/**
 * @file process.c
 * @brief 进程核心管理实现
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/vfs/vfs.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* 全局进程链表 */
struct process *process_list = NULL;
spinlock_t      process_list_lock;

/* PID Bitmap */
static uint32_t *pid_bitmap   = NULL;
static uint32_t  pid_capacity = 0;

/* 内核进程(PID 0) */
struct process kernel_process;

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

    kernel_process.name          = "kernel";
    kernel_process.state         = PROCESS_RUNNING;
    kernel_process.exit_code     = 0;
    kernel_process.page_dir_phys = NULL; /* 内核进程使用当前(或内核)页表 */
    kernel_process.cap_table     = cap_table_create();
    kernel_process.threads       = NULL;
    kernel_process.thread_count  = 0;
    kernel_process.thread_lock   = mutex_create();
    kernel_process.sync_table    = kzalloc(sizeof(struct sync_table));
    kernel_process.parent        = NULL;
    kernel_process.children      = NULL;
    kernel_process.next_sibling  = NULL;
    kernel_process.next          = NULL;
    kernel_process.refcount      = 1;
    kernel_process.cwd[0]        = '/';
    kernel_process.cwd[1]        = '\0';

    if (kernel_process.sync_table) {
        spin_init(&kernel_process.sync_table->lock);
        kernel_process.sync_table->mutex_bitmap = 0;
    }

    process_list = &kernel_process;

    /* 注册 PROCESS 能力类型 */
    cap_register_type(CAP_TYPE_PROCESS, (cap_ref_fn)process_ref, (cap_unref_fn)process_unref);

    pr_info("Process subsystem initialized (kernel PID 0)");
}

void free_pid(pid_t pid) {
    if (pid == 0 || pid >= (int32_t)pid_capacity) {
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
        if (proc->page_dir_phys) {
            const struct mm_operations *mm = mm_get_ops();
            if (mm && mm->destroy_as) {
                mm->destroy_as(proc->page_dir_phys);
            }
            proc->page_dir_phys = NULL;
        }
        if (proc->cap_table) {
            cap_table_destroy(proc->cap_table);
        }
        if (proc->thread_lock) {
            mutex_destroy(proc->thread_lock);
        }
        if (proc->sync_table) {
            for (uint32_t i = 0; i < 32; i++) {
                if (proc->sync_table->mutexes[i]) {
                    mutex_destroy(proc->sync_table->mutexes[i]);
                    proc->sync_table->mutexes[i] = NULL;
                }
            }
            kfree(proc->sync_table);
            proc->sync_table = NULL;
        }
        if (proc->fd_table) {
            fd_table_destroy(proc->fd_table);
            proc->fd_table = NULL;
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

        /* 释放进程名 */
        if (proc->name) {
            kfree((void *)proc->name);
        }

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

    /* 复制进程名(防止外部字符串被释放或覆盖) */
    const char *src       = name ? name : "?";
    size_t      len       = strlen(src);
    char       *name_copy = kmalloc(len + 1);
    if (name_copy) {
        memcpy(name_copy, src, len + 1);
    }
    proc->name      = name_copy; /* NULL 表示分配失败 */
    proc->state     = PROCESS_RUNNING;
    proc->exit_code = 0;

    /* 创建地址空间 */
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->create_as) {
        panic("MM ops not initialized");
    }
    proc->page_dir_phys = mm->create_as();
    if (!proc->page_dir_phys) {
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }
    proc->cap_table    = cap_table_create();
    proc->threads      = NULL;
    proc->thread_count = 0;
    proc->thread_lock  = mutex_create();
    proc->sync_table   = kzalloc(sizeof(struct sync_table));
    proc->parent       = NULL;
    proc->children     = NULL;
    proc->next_sibling = NULL;
    proc->refcount     = 1;
    proc->cwd[0]       = '/';
    proc->cwd[1]       = '\0';

    if (proc->sync_table) {
        spin_init(&proc->sync_table->lock);
        proc->sync_table->mutex_bitmap = 0;
    }

    /* 创建 fd 表 */
    proc->fd_table = fd_table_create();

    if (!proc->cap_table || !proc->thread_lock || !proc->sync_table || !proc->fd_table) {
        if (proc->page_dir_phys) {
            if (mm->destroy_as) {
                mm->destroy_as(proc->page_dir_phys);
            }
        }
        if (proc->cap_table) {
            cap_table_destroy(proc->cap_table);
        }
        if (proc->thread_lock) {
            mutex_destroy(proc->thread_lock);
        }
        if (proc->sync_table) {
            kfree(proc->sync_table);
        }
        if (proc->fd_table) {
            fd_table_destroy(proc->fd_table);
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

    process_unref(proc);
}

void process_add_thread(struct process *proc, struct thread *t) {
    if (!proc || !t) {
        return;
    }

    mutex_lock(proc->thread_lock);

    t->proc_next  = proc->threads;
    proc->threads = t;
    t->owner      = proc;
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
            *pp          = t->proc_next;
            t->proc_next = NULL;
            t->owner     = NULL;
            proc->thread_count--;
            break;
        }
        pp = &(*pp)->proc_next;
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
