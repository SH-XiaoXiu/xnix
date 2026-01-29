/**
 * @file kernel/sys/sys_thread.c
 * @brief 用户线程相关系统调用
 */

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

extern void enter_user_mode(uint32_t eip, uint32_t esp);

#define USER_ADDR_MAX 0xC0000000u

struct user_thread_start {
    uint32_t entry;
    uint32_t arg;
    uint32_t stack_top;
};

static void user_thread_trampoline(void *arg) {
    struct user_thread_start *start = (struct user_thread_start *)arg;
    if (!start) {
        thread_exit(-EINVAL);
    }

    uint32_t user_esp = start->stack_top;
    uint32_t frame[2];
    frame[0] = 0;
    frame[1] = start->arg;

    int ret = copy_to_user((void *)(uintptr_t)(user_esp - sizeof(frame)), frame, sizeof(frame));
    uint32_t entry = start->entry;

    kfree(start);

    if (ret < 0) {
        thread_exit(ret);
    }

    enter_user_mode(entry, user_esp - sizeof(frame));
    thread_exit(-EFAULT);
}

static spinlock_t g_join_lock = SPINLOCK_INIT;

static int32_t sys_thread_create(const uint32_t *args) {
    uint32_t entry     = args[0];
    uint32_t user_arg  = args[1];
    uint32_t stack_top = args[2];

    if (!entry || !stack_top) {
        return -EINVAL;
    }
    if (entry >= USER_ADDR_MAX || stack_top >= USER_ADDR_MAX) {
        return -EFAULT;
    }

    struct process *proc = process_get_current();
    if (!proc || proc->pid == 0) {
        return -EPERM;
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        return -ENOSYS;
    }

    uintptr_t entry_paddr = mm->query(proc->page_dir_phys, (uintptr_t)entry);
    if (!entry_paddr) {
        return -EFAULT;
    }
    uintptr_t sp_paddr = mm->query(proc->page_dir_phys, (uintptr_t)stack_top - 4);
    if (!sp_paddr) {
        return -EFAULT;
    }

    struct user_thread_start *start = kmalloc(sizeof(*start));
    if (!start) {
        return -ENOMEM;
    }

    start->entry     = entry;
    start->arg       = user_arg;
    start->stack_top = stack_top;

    thread_t t = thread_create_with_owner("uthread", user_thread_trampoline, start, proc);
    if (!t) {
        kfree(start);
        return -ENOMEM;
    }

    struct thread *kt  = (struct thread *)t;
    kt->user_stack_top = stack_top;
    process_add_thread(proc, kt);

    return (int32_t)thread_get_tid(t);
}

static int32_t sys_thread_exit(const uint32_t *args) {
    struct thread *current = sched_current();
    if (!current) {
        thread_exit(0);
        return 0;
    }

    current->thread_retval = (void *)(uintptr_t)args[0];

    tid_t joiner_tid = current->joiner_tid;
    if (joiner_tid != TID_INVALID) {
        struct thread *joiner = thread_find_by_tid(joiner_tid);
        if (joiner) {
            sched_wakeup_thread(joiner);
        }
    }

    thread_exit(0);
    return 0;
}

static int32_t sys_thread_join(const uint32_t *args) {
    tid_t    tid        = (tid_t)args[0];
    uint32_t retval_ptr = args[1];

    struct thread  *current = sched_current();
    struct process *proc    = process_get_current();
    if (!current || !proc) {
        return -EINVAL;
    }

    if (tid == current->tid) {
        return -EDEADLK;
    }

    for (;;) {
        struct thread *target = thread_find_by_tid(tid);
        if (!target) {
            return -ESRCH;
        }

        uint32_t flags = spin_lock_irqsave(&g_join_lock);

        if (target->owner != proc) {
            spin_unlock_irqrestore(&g_join_lock, flags);
            return -EINVAL;
        }
        if (target->is_detached || target->has_been_joined ||
            (target->joiner_tid != TID_INVALID && target->joiner_tid != current->tid)) {
            spin_unlock_irqrestore(&g_join_lock, flags);
            return -EINVAL;
        }

        if (target->joiner_tid == TID_INVALID) {
            target->joiner_tid = current->tid;
        }

        if (target->state != THREAD_EXITED) {
            spin_unlock_irqrestore(&g_join_lock, flags);
            sched_block(target);
            continue;
        }

        void *retval = target->thread_retval;
        spin_unlock_irqrestore(&g_join_lock, flags);

        int ret = 0;
        if (retval_ptr) {
            ret = copy_to_user((void *)(uintptr_t)retval_ptr, &retval, sizeof(retval));
        }

        flags                   = spin_lock_irqsave(&g_join_lock);
        target->has_been_joined = true;
        target->joiner_tid      = TID_INVALID;
        spin_unlock_irqrestore(&g_join_lock, flags);

        return ret;
    }
}

static int32_t sys_thread_self(const uint32_t *args) {
    (void)args;
    struct thread *current = sched_current();
    return current ? (int32_t)current->tid : (int32_t)TID_INVALID;
}

static int32_t sys_thread_yield(const uint32_t *args) {
    (void)args;
    thread_yield();
    return 0;
}

static int32_t sys_thread_detach(const uint32_t *args) {
    tid_t tid = (tid_t)args[0];

    struct process *proc = process_get_current();
    if (!proc) {
        return -EINVAL;
    }

    struct thread *target = thread_find_by_tid(tid);
    if (!target) {
        return -ESRCH;
    }

    uint32_t flags = spin_lock_irqsave(&g_join_lock);

    if (target->owner != proc) {
        spin_unlock_irqrestore(&g_join_lock, flags);
        return -EINVAL;
    }
    if (target->is_detached || target->has_been_joined || target->joiner_tid != TID_INVALID) {
        spin_unlock_irqrestore(&g_join_lock, flags);
        return -EINVAL;
    }

    target->is_detached = true;
    spin_unlock_irqrestore(&g_join_lock, flags);
    return 0;
}

void sys_thread_init(void) {
    syscall_register(SYS_THREAD_CREATE, sys_thread_create, 3, "thread_create");
    syscall_register(SYS_THREAD_EXIT, sys_thread_exit, 1, "thread_exit");
    syscall_register(SYS_THREAD_JOIN, sys_thread_join, 2, "thread_join");
    syscall_register(SYS_THREAD_SELF, sys_thread_self, 0, "thread_self");
    syscall_register(SYS_THREAD_YIELD, sys_thread_yield, 0, "thread_yield");
    syscall_register(SYS_THREAD_DETACH, sys_thread_detach, 1, "thread_detach");
}
