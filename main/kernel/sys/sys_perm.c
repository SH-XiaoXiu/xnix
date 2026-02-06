#include <arch/cpu.h>

#include <sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/syscall.h>

/* SYS_PERM_CHECK: ebx=perm_id */
static int32_t sys_perm_check(const uint32_t *args) {
    perm_id_t       id   = (perm_id_t)args[0];
    struct process *proc = (struct process *)process_current();

    if (perm_check(proc, id)) {
        return 0;
    }
    return -EPERM;
}

void sys_perm_init(void) {
    syscall_register(SYS_PERM_CHECK, sys_perm_check, 1, "perm_check");
}
