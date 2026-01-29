/**
 * @file kernel/sys/sys_misc.c
 * @brief 杂项系统调用
 */

#include <kernel/sys/syscall.h>
#include <xnix/boot.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>

extern void sleep_ms(uint32_t ms);

/* SYS_PUTC: ebx=char */
static int32_t sys_putc(const uint32_t *args) {
    kputc((char)(args[0] & 0xFF));
    return 0;
}

/* SYS_SLEEP: ebx=ms */
static int32_t sys_sleep(const uint32_t *args) {
    sleep_ms(args[0]);
    return 0;
}

/* SYS_MODULE_COUNT */
static int32_t sys_module_count(const uint32_t *args) {
    (void)args;
    return (int32_t)boot_get_module_count();
}

/**
 * 注册杂项系统调用
 */
void sys_misc_init(void) {
    syscall_register(SYS_PUTC, sys_putc, 1, "putc");
    syscall_register(SYS_SLEEP, sys_sleep, 1, "sleep");
    syscall_register(SYS_MODULE_COUNT, sys_module_count, 0, "module_count");
}
