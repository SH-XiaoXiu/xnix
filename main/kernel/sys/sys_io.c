/**
 * @file kernel/sys/sys_io.c
 * @brief I/O 端口系统调用
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/io/ioport.h>
#include <kernel/sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/process.h>
#include <xnix/syscall.h>

/* SYS_IOPORT_OUTB: ebx=handle, ecx=port, edx=val */
static int32_t sys_ioport_outb(const uint32_t *args) {
    cap_handle_t    h    = (cap_handle_t)args[0];
    uint16_t        port = (uint16_t)args[1];
    uint8_t         val  = (uint8_t)args[2];
    struct process *proc = (struct process *)process_current();

    struct ioport_range *r = cap_lookup(proc, h, CAP_TYPE_IOPORT, CAP_WRITE);
    if (!r || !ioport_range_contains(r, port)) {
        return -EPERM;
    }

    outb(port, val);
    return 0;
}

/* SYS_IOPORT_INB: ebx=handle, ecx=port */
static int32_t sys_ioport_inb(const uint32_t *args) {
    cap_handle_t    h    = (cap_handle_t)args[0];
    uint16_t        port = (uint16_t)args[1];
    struct process *proc = (struct process *)process_current();

    struct ioport_range *r = cap_lookup(proc, h, CAP_TYPE_IOPORT, CAP_READ);
    if (!r || !ioport_range_contains(r, port)) {
        return -EPERM;
    }

    return (int32_t)inb(port);
}

/**
 * 注册 I/O 系统调用
 */
void sys_io_init(void) {
    syscall_register(SYS_IOPORT_OUTB, sys_ioport_outb, 3, "ioport_outb");
    syscall_register(SYS_IOPORT_INB, sys_ioport_inb, 2, "ioport_inb");
}
