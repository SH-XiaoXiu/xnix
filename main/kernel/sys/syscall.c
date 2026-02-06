/**
 * @file kernel/sys/syscall.c
 * @brief 系统调用分发框架
 */

#include <arch/syscall.h>

#include <sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* 系统调用表 */
static struct syscall_entry syscall_table[CFG_NR_SYSCALLS];

/**
 * 注册系统调用
 */
void syscall_register(uint32_t nr, syscall_fn_t handler, uint8_t nargs, const char *name) {
    if (nr >= CFG_NR_SYSCALLS) {
        pr_err("syscall: nr %u out of range", nr);
        return;
    }
    if (syscall_table[nr].handler != NULL) {
        pr_warn("syscall: nr %u already registered as %s", nr, syscall_table[nr].name);
    }
    syscall_table[nr].handler = handler;
    syscall_table[nr].nargs   = nargs;
    syscall_table[nr].name    = name;
}

/**
 * 系统调用分发(平台无关入口)
 */
struct syscall_result syscall_dispatch(const struct syscall_args *args) {
    struct syscall_result result;
    uint32_t              nr = args->nr;

    if (nr >= CFG_NR_SYSCALLS || syscall_table[nr].handler == NULL) {
        pr_warn("Unknown syscall: %u", nr);
        result.retval = -ENOSYS;
        return result;
    }

    result.retval = syscall_table[nr].handler(args->arg);
    return result;
}

/* 子系统初始化声明 */
extern void sys_ipc_init(void);
extern void sys_process_init(void);
extern void sys_io_init(void);
extern void sys_misc_init(void);
extern void sys_thread_init(void);
extern void sys_sync_init(void);
extern void sys_irq_init(void);
extern void sys_input_init(void);
extern void sys_mm_init(void);
extern void sys_handle_init(void);
extern void sys_perm_init(void);
extern void sys_kmsg_init(void);

/**
 * 初始化系统调用子系统
 */
void syscall_init(void) {
    memset(syscall_table, 0, sizeof(syscall_table));

    sys_ipc_init();
    sys_process_init();
    sys_thread_init();
    sys_sync_init();
    sys_io_init();
    sys_irq_init();
    sys_input_init();
    sys_mm_init();
    sys_misc_init();
    sys_handle_init();
    sys_perm_init();
    sys_kmsg_init();

    pr_info("syscall: initialized %d syscalls", CFG_NR_SYSCALLS);
}
