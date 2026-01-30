/**
 * @file kernel/sys/sys_misc.c
 * @brief 杂项系统调用
 */

#include <kernel/sys/syscall.h>
#include <xnix/boot.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>
#include <xnix/syscall.h>

extern void sleep_ms(uint32_t ms);

/* 使用与 kprintf 相同的锁,避免内核输出和用户输出交错 */
extern spinlock_t kprintf_lock;

/* SYS_PUTC: ebx=char (保留兼容性) */
static int32_t sys_putc(const uint32_t *args) {
    kputc((char)(args[0] & 0xFF));
    return 0;
}

/* SYS_WRITE: ebx=fd, ecx=buf, edx=len */
static int32_t sys_write(const uint32_t *args) {
    int         fd  = (int)args[0];
    const char *buf = (const char *)args[1];
    size_t      len = (size_t)args[2];

    if (fd != 1 && fd != 2) { /* STDOUT/STDERR */
        return -1;
    }

    if (!buf || len == 0) {
        return 0;
    }

    /* 原子输出整个消息(使用 kprintf_lock 避免与内核输出交错) */
    uint32_t flags = spin_lock_irqsave(&kprintf_lock);
    for (size_t i = 0; i < len; i++) {
        kputc(buf[i]);
    }
    spin_unlock_irqrestore(&kprintf_lock, flags);

    return (int32_t)len;
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
    syscall_register(SYS_WRITE, sys_write, 3, "write");
    syscall_register(SYS_SLEEP, sys_sleep, 1, "sleep");
    syscall_register(SYS_MODULE_COUNT, sys_module_count, 0, "module_count");
}
