/**
 * @file kernel/sys/sys_process.c
 * @brief 进程相关系统调用
 */

#include <kernel/sys/syscall.h>
#include <kernel/process/process.h>
#include <xnix/boot.h>
#include <xnix/errno.h>
#include <xnix/process.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>
#include <xnix/vmm.h>

extern void thread_exit(int code);

/* 用户态 spawn 参数结构(与用户态定义一致) */
struct user_spawn_cap {
    uint32_t src;
    uint32_t rights;
    uint32_t dst_hint;
};

struct user_spawn_args {
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

/* SYS_SPAWN: ebx=args */
static int32_t sys_spawn(const uint32_t *args) {
    struct user_spawn_args *user_args = (struct user_spawn_args *)(uintptr_t)args[0];
    struct user_spawn_args  kargs;

    int ret = copy_from_user(&kargs, user_args, sizeof(kargs));
    if (ret < 0) {
        return ret;
    }

    /* 获取模块数据 */
    void    *mod_addr = NULL;
    uint32_t mod_size = 0;
    ret = boot_get_module(kargs.module_index, &mod_addr, &mod_size);
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

    /*
     * 切换到内核页表执行 spawn
     *
     * process_spawn_module_ex 需要访问:
     * 1. 模块 ELF 数据(通过恒等映射)
     * 2. vmm_create_pd 中的临时映射
     */
    uint32_t user_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(user_cr3));
    void *kernel_pd = vmm_get_kernel_pd();
    vmm_switch_pd(kernel_pd);

    pid_t pid = process_spawn_module_ex(NULL, mod_addr, mod_size, inherit_caps, cap_count);

    /* 切换回用户页表 */
    vmm_switch_pd((void *)(uintptr_t)user_cr3);

    if (pid == PID_INVALID) {
        return -ENOMEM;
    }
    return (int32_t)pid;
}

/**
 * 注册进程系统调用
 */
void sys_process_init(void) {
    syscall_register(SYS_EXIT,  sys_exit,  1, "exit");
    syscall_register(SYS_SPAWN, sys_spawn, 1, "spawn");
}
