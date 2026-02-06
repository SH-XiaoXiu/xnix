/**
 * @file kernel/sys/sys_misc.c
 * @brief 杂项系统调用(编号:900-999)
 */

#include <sys/syscall.h>
#include <xnix/early_console.h>
#include <xnix/errno.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>

extern void sleep_ms(uint32_t ms);

/* SYS_SLEEP: ebx=ms */
static int32_t sys_sleep(const uint32_t *args) {
    sleep_ms(args[0]);
    return 0;
}

#ifdef CONFIG_DEBUG_CONSOLE
#include <xnix/perm.h>
#include <xnix/process.h>

/* SYS_DEBUG_PUT: ebx=char (仅编译时启用,需要 xnix.debug.console 权限) */
static int32_t sys_debug_put(const uint32_t *args) {
    struct process *proc = (struct process *)process_current();

    /* 检查权限 */
    if (!perm_check_name(proc, "xnix.debug.console")) {
        return -EPERM;
    }

    char c = (char)args[0];
    early_putc(c);
    return 0;
}
#endif

void sys_misc_init(void) {
    syscall_register(SYS_SLEEP, sys_sleep, 1, "sleep");
#ifdef CONFIG_DEBUG_CONSOLE
    syscall_register(SYS_DEBUG_PUT, sys_debug_put, 1, "debug_put");
#endif
}
