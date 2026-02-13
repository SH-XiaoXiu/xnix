/**
 * @file kernel/sys/sys_process.c
 * @brief 进程相关系统调用
 */

#include <arch/cpu.h>

#include <process/process_internal.h>
#include <sched/sched_internal.h>
#include <sys/syscall.h>
#include <xnix/abi/process.h>
#include <xnix/boot.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/percpu.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>
#include <xnix/syscall.h>
#include <xnix/thread_def.h>
#include <xnix/usraccess.h>

extern void thread_exit(int code);

/* 用户态 spawn 参数结构(使用 ABI 定义) */
/* 使用 xnix/abi/handle.h 中的 struct spawn_handle */
/* 使用 xnix/abi/process.h 中的 struct abi_spawn_args */

/* SYS_EXIT: ebx=code */
static int32_t sys_exit(const uint32_t *args) {
    thread_exit((int)args[0]);
    /* 不会返回 */
    return 0;
}

/* SYS_WAITPID: ebx=pid, ecx=status_ptr, edx=options */
static int32_t sys_waitpid(const uint32_t *args) {
    pid_t pid     = (pid_t)args[0];
    int  *user_st = (int *)(uintptr_t)args[1];
    int   options = (int)args[2];

    int   status;
    pid_t ret = process_waitpid(pid, &status, options);

    if (ret > 0 && user_st) {
        int err = copy_to_user(user_st, &status, sizeof(status));
        if (err < 0) {
            return err;
        }
    }

    return (int32_t)ret;
}

/* SYS_GETPID */
static int32_t sys_getpid(const uint32_t *args) {
    (void)args;
    struct process *proc = process_get_current();
    return proc ? (int32_t)proc->pid : 0;
}

/* SYS_GETPPID */
static int32_t sys_getppid(const uint32_t *args) {
    (void)args;
    struct process *proc = process_get_current();
    if (!proc || !proc->parent) {
        return 0;
    }
    return (int32_t)proc->parent->pid;
}

/* SYS_KILL: ebx=pid, ecx=sig */
static int32_t sys_kill(const uint32_t *args) {
    pid_t pid = (pid_t)args[0];
    int   sig = (int)args[1];
    return process_kill(pid, sig);
}

/* SYS_EXEC: ebx=abi_exec_image_args* */
static int32_t sys_exec(const uint32_t *args) {
    struct abi_exec_image_args *user_args = (struct abi_exec_image_args *)(uintptr_t)args[0];
    struct process             *proc      = process_get_current();

    if (!perm_check_name(proc, PERM_NODE_PROCESS_EXEC)) {
        return -EPERM;
    }

    struct abi_exec_image_args *kargs = kmalloc(sizeof(*kargs));
    if (!kargs) {
        return -ENOMEM;
    }

    int ret = copy_from_user(kargs, user_args, sizeof(*kargs));
    if (ret < 0) {
        kfree(kargs);
        return ret;
    }

    kargs->name[ABI_PROC_NAME_MAX - 1] = '\0';

    if (kargs->elf_ptr == 0 || kargs->elf_size == 0) {
        kfree(kargs);
        return -EINVAL;
    }

    if (kargs->elf_size > (16u * 1024u * 1024u)) {
        kfree(kargs);
        return -EINVAL;
    }

    uint32_t page_count = (kargs->elf_size + PAGE_SIZE - 1) / PAGE_SIZE;
    void    *elf_paddr  = alloc_pages(page_count);
    if (!elf_paddr) {
        kfree(kargs);
        return -ENOMEM;
    }

    void *elf_kvirt = PHYS_TO_VIRT((paddr_t)(uintptr_t)elf_paddr);

    ret = copy_from_user(elf_kvirt, (const void *)(uintptr_t)kargs->elf_ptr, kargs->elf_size);
    if (ret < 0) {
        free_pages(elf_paddr, page_count);
        kfree(kargs);
        return ret;
    }

    uint8_t *elf_bytes = (uint8_t *)elf_kvirt;
    if (!(elf_bytes[0] == 0x7F && elf_bytes[1] == 'E' && elf_bytes[2] == 'L' &&
          elf_bytes[3] == 'F')) {
        pr_err("exec: bad magic %02x %02x %02x %02x", elf_bytes[0], elf_bytes[1], elf_bytes[2],
               elf_bytes[3]);
        free_pages(elf_paddr, page_count);
        kfree(kargs);
        return -EINVAL;
    }

    uint32_t handle_count = kargs->handle_count;
    if (handle_count > ABI_EXEC_MAX_HANDLES) {
        handle_count = ABI_EXEC_MAX_HANDLES;
    }

    struct spawn_handle handles[ABI_EXEC_MAX_HANDLES];
    for (uint32_t i = 0; i < handle_count; i++) {
        handles[i].src = (handle_t)kargs->handles[i].src;
        strncpy(handles[i].name, kargs->handles[i].name, sizeof(handles[i].name));
        handles[i].name[sizeof(handles[i].name) - 1] = '\0';
    }

    int argc = (int)kargs->argc;
    if (argc < 0) {
        argc = 0;
    }
    if (argc > ABI_EXEC_MAX_ARGS) {
        argc = ABI_EXEC_MAX_ARGS;
    }

    uint32_t flags = kargs->flags;

    /* 权限处理 */
    struct perm_profile *profile;
    if (flags & ABI_EXEC_INHERIT_PERM) {
        /* 继承父进程 profile */
        profile = proc->perms ? proc->perms->profile : NULL;
    } else if (kargs->profile_name[0] != '\0') {
        profile = perm_profile_find(kargs->profile_name);
        if (!profile) {
            kprintf("[sys_exec] WARNING: Profile '%s' not found for process '%s'\n",
                    kargs->profile_name, kargs->name);
        }
        /* 权限降级检查:子进程 profile 不能超过父进程权限 */
        if (profile && proc->perms && !perm_profile_is_subset(profile, proc->perms)) {
            free_pages(elf_paddr, page_count);
            kfree(kargs);
            return -EPERM;
        }
    } else {
        /* profile_name 为空:继承父进程 profile */
        profile = proc->perms ? proc->perms->profile : NULL;
    }

    pid_t pid = process_spawn(kargs->name, elf_paddr, kargs->elf_size, handles, handle_count,
                              profile, argc, kargs->argv, flags);
    free_pages(elf_paddr, page_count);
    kfree(kargs);

    if (pid == PID_INVALID) {
        return -EINVAL;
    }
    return (int32_t)pid;
}

/* SYS_PROCLIST: ebx=proclist_args* */
static int32_t sys_proclist(const uint32_t *args) {
    struct abi_proclist_args *user_args = (struct abi_proclist_args *)(uintptr_t)args[0];
    struct abi_proclist_args  kargs;

    /* 从用户空间拷贝参数 */
    int ret = copy_from_user(&kargs, user_args, sizeof(kargs));
    if (ret < 0) {
        return ret;
    }

    /* 验证参数 */
    if (!kargs.buf || kargs.buf_count == 0) {
        return -EINVAL;
    }

    /* 限制单次返回数量 */
    uint32_t count = kargs.buf_count;
    if (count > ABI_PROCLIST_MAX) {
        count = ABI_PROCLIST_MAX;
    }

    /* 填充系统信息(如果请求) */
    if (kargs.sys_info) {
        struct abi_sys_info sys_info;
        sys_info.cpu_count   = percpu_cpu_count();
        sys_info.total_ticks = sched_get_global_ticks();
        sys_info.idle_ticks  = sched_get_idle_ticks();

        ret = copy_to_user(kargs.sys_info, &sys_info, sizeof(sys_info));
        if (ret < 0) {
            return ret;
        }
    }

    /* 遍历进程链表 */
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    struct process *proc    = process_list;
    uint32_t        index   = 0;
    uint32_t        written = 0;

    /* 跳过 start_index 之前的进程 */
    while (proc && index < kargs.start_index) {
        proc = proc->next;
        index++;
    }

    /* 填充进程信息 */
    while (proc && written < count) {
        struct abi_proc_info info;
        info.pid          = (int32_t)proc->pid;
        info.ppid         = proc->parent ? (int32_t)proc->parent->pid : 0;
        info.state        = (uint8_t)proc->state;
        info.reserved[0]  = 0;
        info.reserved[1]  = 0;
        info.reserved[2]  = 0;
        info.thread_count = proc->thread_count;

        /* 累计所有线程的 CPU ticks */
        info.cpu_ticks   = 0;
        struct thread *t = proc->threads;
        while (t) {
            info.cpu_ticks += t->cpu_ticks;
            t = t->proc_next;
        }

        /* 计算堆内存使用 */
        uint32_t heap_used = 0;
        if (proc->heap_current > proc->heap_start) {
            heap_used = proc->heap_current - proc->heap_start;
        }
        info.heap_kb = heap_used / 1024;

        /* 栈内存 */
        info.stack_kb = (proc->stack_pages * 4096) / 1024;

        /* 拷贝进程名 */
        if (proc->name) {
            size_t name_len = 0;
            while (proc->name[name_len] && name_len < ABI_PROC_NAME_MAX - 1) {
                info.name[name_len] = proc->name[name_len];
                name_len++;
            }
            info.name[name_len] = '\0';
        } else {
            info.name[0] = '\0';
        }

        /* 拷贝到用户空间 */
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        ret = copy_to_user(&kargs.buf[written], &info, sizeof(info));

        flags = cpu_irq_save();
        spin_lock(&process_list_lock);

        if (ret < 0) {
            spin_unlock(&process_list_lock);
            cpu_irq_restore(flags);
            return ret;
        }

        proc = proc->next;
        written++;
    }

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);

    return (int32_t)written;
}

void sys_process_init(void) {
    syscall_register(SYS_EXIT, sys_exit, 1, "exit");
    syscall_register(SYS_WAITPID, sys_waitpid, 3, "waitpid");
    syscall_register(SYS_GETPID, sys_getpid, 0, "getpid");
    syscall_register(SYS_GETPPID, sys_getppid, 0, "getppid");
    syscall_register(SYS_KILL, sys_kill, 2, "kill");
    syscall_register(SYS_EXEC, sys_exec, 1, "exec");
    syscall_register(SYS_PROCLIST, sys_proclist, 1, "proclist");
}
