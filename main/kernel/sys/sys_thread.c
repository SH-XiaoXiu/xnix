/**
 * @file kernel/sys/sys_thread.c
 * @brief 用户线程系统调用实现
 *
 * 实现 POSIX 风格的用户态多线程支持(pthread),包括线程创建,退出,
 * join,detach 等核心功能.采用 1:1 线程模型(用户线程直接对应内核线程).
 * - 用户栈由 libpthread 分配,内核只验证地址合法性
 * - 通过跳板函数 user_thread_trampoline 从内核态切换到用户态
 * - join 使用 wait_chan 机制阻塞等待,退出时唤醒 joiner
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

/* 用户地址空间上限(内核空间起始地址) */
#define USER_ADDR_MAX 0xC0000000u

/**
 * 用户线程启动参数
 *
 * 通过 kmalloc 在内核堆分配,传递给跳板函数后立即释放.
 */
struct user_thread_start {
    uint32_t entry;     /* 用户态入口函数地址 */
    uint32_t arg;       /* 传递给入口函数的参数 */
    uint32_t stack_top; /* 用户态栈顶地址 */
};

/**
 * 用户线程跳板函数
 *
 * 内核线程的入口函数,负责切换到用户态并执行用户线程代码.
 * 执行流程:
 * 1. 在用户栈上构造调用帧(返回地址 + 参数)
 * 2. 通过 enter_user_mode 切换到用户态
 * 3. 用户态入口函数开始执行
 *
 * @param arg struct user_thread_start* 启动参数
 */
static void user_thread_trampoline(void *arg) {
    struct user_thread_start *start = (struct user_thread_start *)arg;
    if (!start) {
        thread_exit(-EINVAL);
    }

    uint32_t user_esp = start->stack_top;
    uint32_t frame[2];
    frame[0] = 0;          /* 返回地址(线程不应返回) */
    frame[1] = start->arg; /* 第一个参数 */

    /* 将调用帧拷贝到用户栈 */
    int ret = copy_to_user((void *)(uintptr_t)(user_esp - sizeof(frame)), frame, sizeof(frame));
    uint32_t entry = start->entry;

    kfree(start); /* 参数已拷贝,释放内核堆内存 */

    if (ret < 0) {
        thread_exit(ret);
    }

    /* 切换到用户态,开始执行用户代码 */
    enter_user_mode(entry, user_esp - sizeof(frame));

    /* 不应到达此处 */
    thread_exit(-EFAULT);
}

/* 保护 join 相关字段(is_detached, has_been_joined, joiner_tid)的全局锁 */
static spinlock_t g_join_lock = SPINLOCK_INIT;

/**
 * SYS_THREAD_CREATE - 创建用户线程
 *
 * @param args[0] entry     用户态入口函数地址
 * @param args[1] user_arg  传递给入口函数的参数
 * @param args[2] stack_top 用户态栈顶地址
 * @return 新线程的 TID,失败返回负错误码
 */
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

    /* 验证用户地址已映射且可访问 */
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        return -ENOSYS;
    }

    uintptr_t entry_paddr = mm->query(proc->page_dir_phys, (uintptr_t)entry);
    if (!entry_paddr) {
        return -EFAULT; /* 入口地址未映射 */
    }
    uintptr_t sp_paddr = mm->query(proc->page_dir_phys, (uintptr_t)stack_top - 4);
    if (!sp_paddr) {
        return -EFAULT; /* 栈地址未映射 */
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

    struct thread *kt = (struct thread *)t;
    kt->ustack_top    = stack_top;
    process_add_thread(proc, kt);

    return (int32_t)thread_get_tid(t);
}

/**
 * SYS_THREAD_EXIT - 退出当前线程
 *
 * 保存退出值并唤醒等待的 joiner,然后终止当前线程.
 *
 * @param args[0] retval 退出值,供 pthread_join 获取
 * @return 不返回
 */
static int32_t sys_thread_exit(const uint32_t *args) {
    struct thread *current = sched_current();
    if (!current) {
        thread_exit(0);
        return 0;
    }

    /* 保存退出值供 join 获取 */
    current->thread_retval = (void *)(uintptr_t)args[0];

    /* 唤醒等待的 joiner */
    tid_t joiner_tid = current->joiner_tid;
    if (joiner_tid != TID_INVALID) {
        struct thread *joiner = thread_find_by_tid(joiner_tid);
        if (joiner) {
            sched_wakeup_thread(joiner);
        }
    }

    thread_exit(0);
    return 0; /* 不会到达 */
}

/**
 * SYS_THREAD_JOIN - 等待线程退出
 *
 * 阻塞等待目标线程退出,并获取其退出值.
 *
 * 错误检查:
 * - EDEADLK: 尝试 join 自己
 * - ESRCH: 目标线程不存在
 * - EINVAL: 目标线程不属于当前进程,已被 detach,已被 join,或有其他 joiner
 *
 * 循环逻辑: 线程可能尚未退出,需要阻塞等待.被唤醒后重新检查状态.
 *
 * @param args[0] tid        目标线程 TID
 * @param args[1] retval_ptr 用户空间指针,用于接收退出值(可为 NULL)
 * @return 0 成功,负错误码失败
 */
static int32_t sys_thread_join(const uint32_t *args) {
    tid_t    tid        = (tid_t)args[0];
    uint32_t retval_ptr = args[1];

    struct thread  *current = sched_current();
    struct process *proc    = process_get_current();
    if (!current || !proc) {
        return -EINVAL;
    }

    if (tid == current->tid) {
        return -EDEADLK; /* 不能 join 自己 */
    }

    /* 循环等待直到线程退出 */
    for (;;) {
        struct thread *target = thread_find_by_tid(tid);
        if (!target) {
            return -ESRCH; /* 线程不存在 */
        }

        uint32_t flags = spin_lock_irqsave(&g_join_lock);

        /* 只能 join 同一进程的线程 */
        if (target->owner != proc) {
            spin_unlock_irqrestore(&g_join_lock, flags);
            return -EINVAL;
        }

        /* 检查线程是否可 join */
        if (target->is_detached || target->has_been_joined ||
            (target->joiner_tid != TID_INVALID && target->joiner_tid != current->tid)) {
            spin_unlock_irqrestore(&g_join_lock, flags);
            return -EINVAL;
        }

        /* 记录 joiner,用于退出时唤醒 */
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

/**
 * SYS_THREAD_YIELD - 主动让出 CPU
 *
 * 将当前线程移到运行队列末尾,让其他线程有机会执行.
 *
 * @return 0
 */
static int32_t sys_thread_yield(const uint32_t *args) {
    (void)args;
    thread_yield();
    return 0;
}

/**
 * SYS_THREAD_DETACH - 分离线程
 *
 * 将线程标记为 detached 状态,退出时自动回收资源,无需 join.
 *
 * 错误检查:
 * - ESRCH: 目标线程不存在
 * - EINVAL: 目标线程不属于当前进程,已被 detach,已被 join,或有 joiner 等待
 *
 * @param args[0] tid 目标线程 TID
 * @return 0 成功,负错误码失败
 */
static int32_t sys_thread_detach(const uint32_t *args) {
    tid_t tid = (tid_t)args[0];

    struct process *proc = process_get_current();
    if (!proc) {
        return -EINVAL;
    }

    struct thread *target = thread_find_by_tid(tid);
    if (!target) {
        return -ESRCH; /* 线程不存在 */
    }

    uint32_t flags = spin_lock_irqsave(&g_join_lock);

    /* 只能 detach 同一进程的线程 */
    if (target->owner != proc) {
        spin_unlock_irqrestore(&g_join_lock, flags);
        return -EINVAL;
    }

    /* 检查线程状态 */
    if (target->is_detached || target->has_been_joined || target->joiner_tid != TID_INVALID) {
        spin_unlock_irqrestore(&g_join_lock, flags);
        return -EINVAL;
    }

    target->is_detached = true;
    spin_unlock_irqrestore(&g_join_lock, flags);
    return 0;
}

/**
 * 注册线程管理系统调用
 */
void sys_thread_init(void) {
    syscall_register(SYS_THREAD_CREATE, sys_thread_create, 3, "thread_create");
    syscall_register(SYS_THREAD_EXIT, sys_thread_exit, 1, "thread_exit");
    syscall_register(SYS_THREAD_JOIN, sys_thread_join, 2, "thread_join");
    syscall_register(SYS_THREAD_SELF, sys_thread_self, 0, "thread_self");
    syscall_register(SYS_THREAD_YIELD, sys_thread_yield, 0, "thread_yield");
    syscall_register(SYS_THREAD_DETACH, sys_thread_detach, 1, "thread_detach");
}
