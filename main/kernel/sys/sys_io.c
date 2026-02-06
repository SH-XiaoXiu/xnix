/**
 * @file kernel/sys/sys_io.c
 * @brief I/O 端口系统调用
 */

#include <arch/cpu.h>

#include <sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/syscall.h>

/* SYS_IOPORT_OUTB: ebx=port, ecx=val */
static int32_t sys_ioport_outb(const uint32_t *args) {
    uint16_t        port = (uint16_t)args[0];
    uint8_t         val  = (uint8_t)args[1];
    struct process *proc = (struct process *)process_current();

    if (!perm_check_ioport(proc, port)) {
        return -EPERM;
    }

    outb(port, val);
    return 0;
}

/* SYS_IOPORT_INB: ebx=port */
static int32_t sys_ioport_inb(const uint32_t *args) {
    uint16_t        port = (uint16_t)args[0];
    struct process *proc = (struct process *)process_current();

    if (!perm_check_ioport(proc, port)) {
        return -EPERM;
    }

    return (int32_t)inb(port);
}

/* SYS_IOPORT_OUTW: ebx=port, ecx=val */
static int32_t sys_ioport_outw(const uint32_t *args) {
    uint16_t        port = (uint16_t)args[0];
    uint16_t        val  = (uint16_t)args[1];
    struct process *proc = (struct process *)process_current();

    if (!perm_check_ioport(proc, port)) {
        return -EPERM;
    }

    outw(port, val);
    return 0;
}

/* SYS_IOPORT_INW: ebx=port */
static int32_t sys_ioport_inw(const uint32_t *args) {
    uint16_t        port = (uint16_t)args[0];
    struct process *proc = (struct process *)process_current();

    if (!perm_check_ioport(proc, port)) {
        return -EPERM;
    }

    return (int32_t)inw(port);
}

/**
 * 注册 I/O 系统调用
 */
void sys_io_init(void) {
    syscall_register(SYS_IOPORT_OUTB, sys_ioport_outb, 2, "ioport_outb");
    syscall_register(SYS_IOPORT_INB, sys_ioport_inb, 1, "ioport_inb");
    syscall_register(SYS_IOPORT_OUTW, sys_ioport_outw, 2, "ioport_outw");
    syscall_register(SYS_IOPORT_INW, sys_ioport_inw, 1, "ioport_inw");
}
