/**
 * @file kernel/sys/sys_process.c
 * @brief 进程相关系统调用
 */

#include <kernel/process/process.h>
#include <kernel/sys/syscall.h>
#include <kernel/vfs/vfs.h>
#include <xnix/abi/process.h>
#include <xnix/boot.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/string.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

extern void thread_exit(int code);

/* 用户态 spawn 参数结构(与用户态定义一致) */
struct user_spawn_cap {
    uint32_t src;
    uint32_t rights;
    uint32_t dst_hint;
};

struct user_spawn_args {
    char                  name[16];
    uint32_t              module_index;
    uint32_t              cap_count;
    struct user_spawn_cap caps[8];
};

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

/* SYS_SPAWN: ebx=args */
static int32_t sys_spawn(const uint32_t *args) {
    struct user_spawn_args *user_args = (struct user_spawn_args *)(uintptr_t)args[0];
    struct user_spawn_args  kargs;

    int ret = copy_from_user(&kargs, user_args, sizeof(kargs));
    if (ret < 0) {
        return ret;
    }

    kargs.name[sizeof(kargs.name) - 1] = '\0';

    /* 获取模块数据 */
    void    *mod_addr = NULL;
    uint32_t mod_size = 0;
    ret               = boot_get_module(kargs.module_index, &mod_addr, &mod_size);
    if (ret < 0) {
        return -EINVAL;
    }

    /* 构建 capability 继承列表 */
    uint32_t cap_count = kargs.cap_count;
    if (cap_count > 8) {
        cap_count = 8;
    }

    struct spawn_inherit_cap inherit_caps[8];
    for (uint32_t i = 0; i < cap_count; i++) {
        inherit_caps[i].src          = (cap_handle_t)kargs.caps[i].src;
        inherit_caps[i].rights       = kargs.caps[i].rights;
        inherit_caps[i].expected_dst = (cap_handle_t)kargs.caps[i].dst_hint;
    }

    pid_t pid = process_spawn_module_ex(kargs.name, mod_addr, mod_size, inherit_caps, cap_count);

    if (pid == PID_INVALID) {
        return -ENOMEM;
    }
    return (int32_t)pid;
}

/* SYS_EXEC: ebx=exec_args* */
static int32_t sys_exec(const uint32_t *args) {
    struct abi_exec_args *user_args = (struct abi_exec_args *)(uintptr_t)args[0];

    /* 在堆上分配,避免栈溢出(struct abi_exec_args 约 4KB) */
    struct abi_exec_args *kargs = kmalloc(sizeof(struct abi_exec_args));
    if (!kargs) {
        return -ENOMEM;
    }

    /* 从用户空间拷贝参数 */
    int ret = copy_from_user(kargs, user_args, sizeof(*kargs));
    if (ret < 0) {
        kfree(kargs);
        return ret;
    }

    /* 确保路径和参数以 null 结尾 */
    kargs->path[ABI_EXEC_PATH_MAX - 1] = '\0';
    if (kargs->argc < 0 || kargs->argc > ABI_EXEC_MAX_ARGS) {
        kfree(kargs);
        return -EINVAL;
    }
    for (int i = 0; i < kargs->argc; i++) {
        kargs->argv[i][ABI_EXEC_MAX_ARG_LEN - 1] = '\0';
    }

    /* 从文件系统加载 ELF */
    void    *elf_data = NULL;
    uint32_t elf_size = 0;
    ret               = vfs_load_file(kargs->path, &elf_data, &elf_size);
    if (ret < 0) {
        kfree(kargs);
        return ret;
    }

    /*
     * vfs_load_file 返回内核虚拟地址(kmalloc)
     * process_load_elf 期望物理地址
     * 内核直接映射: vaddr 0xC0000000+ -> paddr 0x0+
     */
    void *elf_paddr = (void *)((uintptr_t)elf_data - 0xC0000000);

    /* 从路径提取程序名(最后一个 / 之后的部分) */
    const char *name = kargs->path;
    for (const char *p = kargs->path; *p; p++) {
        if (*p == '/') {
            name = p + 1;
        }
    }

    /* 如果有 .elf 后缀则去掉 */
    char name_buf[32];
    int  name_len = 0;
    while (name[name_len] && name[name_len] != '.' && name_len < 31) {
        name_buf[name_len] = name[name_len];
        name_len++;
    }
    name_buf[name_len] = '\0';

    /* 创建进程(带 argv),使用物理地址 */
    pid_t pid =
        process_spawn_elf_with_args(name_buf, elf_paddr, elf_size, kargs->argc, kargs->argv);

    /* 释放资源 */
    kfree(elf_data);
    kfree(kargs);

    if (pid == PID_INVALID) {
        return -ENOMEM;
    }
    return (int32_t)pid;
}

/**
 * 注册进程系统调用
 */
void sys_process_init(void) {
    syscall_register(SYS_EXIT, sys_exit, 1, "exit");
    syscall_register(SYS_SPAWN, sys_spawn, 1, "spawn");
    syscall_register(SYS_WAITPID, sys_waitpid, 3, "waitpid");
    syscall_register(SYS_GETPID, sys_getpid, 0, "getpid");
    syscall_register(SYS_GETPPID, sys_getppid, 0, "getppid");
    syscall_register(SYS_KILL, sys_kill, 2, "kill");
    syscall_register(SYS_EXEC, sys_exec, 1, "exec");
}
