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
#include <xnix/usraccess.h>

/* SYS_DEBUG_WRITE: ebx=buf_ptr, ecx=len (仅编译时启用,需要 xnix.debug.console 权限) */
static int32_t sys_debug_write(const uint32_t *args) {
    struct process *proc = (struct process *)process_current();

    /* 检查权限 */
    if (!perm_check_name(proc, "xnix.debug.console")) {
        return -EPERM;
    }

    const void *user_buf = (const void *)args[0];
    uint32_t    len      = args[1];
    if (!user_buf || len == 0) {
        return 0;
    }

    if (len > 512) {
        len = 512;
    }

    char buf[513];
    int  ret = copy_from_user(buf, user_buf, (size_t)len);
    if (ret < 0) {
        return ret;
    }
    buf[len] = '\0';

    early_puts(buf);
    return (int32_t)len;
}

/* SYS_DEBUG_SET_COLOR: ebx=fg, ecx=bg */
static int32_t sys_debug_set_color(const uint32_t *args) {
    struct process *proc = (struct process *)process_current();

    if (!perm_check_name(proc, "xnix.debug.console")) {
        return -EPERM;
    }

    early_console_set_color((enum early_console_color)(args[0] & 0x0F),
                            (enum early_console_color)(args[1] & 0x0F));
    return 0;
}

/* SYS_DEBUG_RESET_COLOR */
static int32_t sys_debug_reset_color(const uint32_t *args) {
    (void)args;
    struct process *proc = (struct process *)process_current();

    if (!perm_check_name(proc, "xnix.debug.console")) {
        return -EPERM;
    }

    early_console_reset_color();
    return 0;
}
#endif

void sys_misc_init(void) {
    syscall_register(SYS_SLEEP, sys_sleep, 1, "sleep");
#ifdef CONFIG_DEBUG_CONSOLE
    syscall_register(SYS_DEBUG_WRITE, sys_debug_write, 2, "debug_write");
    syscall_register(SYS_DEBUG_SET_COLOR, sys_debug_set_color, 2, "debug_set_color");
    syscall_register(SYS_DEBUG_RESET_COLOR, sys_debug_reset_color, 0, "debug_reset_color");
#endif
}
