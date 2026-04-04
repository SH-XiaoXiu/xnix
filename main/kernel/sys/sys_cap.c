/**
 * @file sys_perm.c
 * @brief 能力 (Capability) 相关系统调用
 *
 */

#include <sys/syscall.h>
#include <xnix/cap.h>
#include <xnix/errno.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/syscall.h>

static int32_t sys_cap_check(const uint32_t *args) {
    uint32_t        cap_bit = args[0];
    struct process *proc    = (struct process *)process_current();
    return cap_check(proc, cap_bit) ? 0 : -EPERM;
}

static int32_t sys_cap_grant(const uint32_t *args) {
    pid_t           pid      = (pid_t)args[0];
    uint32_t        cap_bits = args[1];
    struct process *caller   = (struct process *)process_current();

    if (!cap_check(caller, CAP_CAP_DELEGATE)) {
        return -EPERM;
    }
    if (!cap_is_subset(cap_bits, caller->cap_mask)) {
        return -EPERM;
    }

    struct process *target = process_find_by_pid(pid);
    if (!target) {
        return -ENOENT;
    }

    target->cap_mask |= cap_bits;
    process_unref(target);
    return 0;
}

static int32_t sys_cap_revoke(const uint32_t *args) {
    pid_t           pid      = (pid_t)args[0];
    uint32_t        cap_bits = args[1];
    struct process *caller   = (struct process *)process_current();

    if (!cap_check(caller, CAP_CAP_DELEGATE)) {
        return -EPERM;
    }

    struct process *target = process_find_by_pid(pid);
    if (!target) {
        return -ENOENT;
    }

    target->cap_mask &= ~cap_bits;
    process_unref(target);
    return 0;
}

static int32_t sys_cap_query(const uint32_t *args) {
    (void)args;
    struct process *proc = (struct process *)process_current();
    return (int32_t)proc->cap_mask;
}

void sys_cap_init(void) {
    syscall_register(SYS_CAP_CHECK, sys_cap_check, 1, "cap_check");
    syscall_register(SYS_CAP_GRANT, sys_cap_grant, 2, "cap_grant");
    syscall_register(SYS_CAP_REVOKE, sys_cap_revoke, 2, "cap_revoke");
    syscall_register(SYS_CAP_QUERY, sys_cap_query, 2, "cap_query");
}
