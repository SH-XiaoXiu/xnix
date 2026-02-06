#include <arch/cpu.h>

#include <sys/syscall.h>
#include <xnix/abi/syscall.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

/* SYS_HANDLE_CLOSE: ebx=handle */
static int32_t sys_handle_close(const uint32_t *args) {
    handle_t        h    = (handle_t)args[0];
    struct process *proc = (struct process *)process_current();

    handle_free(proc, h);
    return 0;
}

/* SYS_HANDLE_DUPLICATE: ebx=src_handle, ecx=dst_hint, edx=name */
/* 注意:系统调用参数为 uint32_t name 指针需要进行类型转换. */
static int32_t sys_handle_duplicate(const uint32_t *args) {
    handle_t        src_h    = (handle_t)args[0];
    handle_t        dst_hint = (handle_t)args[1];
    const char     *name     = (const char *)(uintptr_t)args[2];
    struct process *proc     = (struct process *)process_current();

    /* 简单的自我复制 */
    char        name_buf[HANDLE_NAME_MAX];
    const char *final_name = NULL;

    if (name) {
        int ret = copy_from_user(name_buf, name, sizeof(name_buf));
        if (ret < 0) {
            return ret;
        }
        name_buf[HANDLE_NAME_MAX - 1] = '\0';
        final_name                    = name_buf;
    }

    handle_t dst_h = handle_transfer(proc, src_h, proc, final_name, dst_hint);
    if (dst_h == HANDLE_INVALID) {
        return -EINVAL; /* 或 ENOMEM */
    }

    return (int32_t)dst_h;
}

/* SYS_HANDLE_GRANT: ebx=pid, ecx=src_handle, edx=name */
static int32_t sys_handle_grant(const uint32_t *args) {
    pid_t           pid   = (pid_t)args[0];
    handle_t        src_h = (handle_t)args[1];
    const char     *name  = (const char *)(uintptr_t)args[2];
    struct process *src   = (struct process *)process_current();

    if (!perm_check_name(src, PERM_NODE_HANDLE_GRANT)) {
        return -EPERM;
    }

    struct process *dst = process_find_by_pid(pid);
    if (!dst) {
        return -ENOENT;
    }

    char        name_buf[HANDLE_NAME_MAX];
    const char *final_name = NULL;
    if (name) {
        int ret = copy_from_user(name_buf, name, sizeof(name_buf));
        if (ret < 0) {
            process_unref(dst);
            return ret;
        }
        name_buf[HANDLE_NAME_MAX - 1] = '\0';
        final_name                    = name_buf;
    }

    handle_t dst_h = handle_transfer(src, src_h, dst, final_name, HANDLE_INVALID);
    process_unref(dst);
    if (dst_h == HANDLE_INVALID) {
        return -EINVAL;
    }

    return (int32_t)dst_h;
}

/* SYS_HANDLE_FIND: ebx=name */
static int32_t sys_handle_find(const uint32_t *args) {
    const char     *name = (const char *)(uintptr_t)args[0];
    struct process *proc = (struct process *)process_current();

    char name_buf[HANDLE_NAME_MAX];
    if (name) {
        int ret = copy_from_user(name_buf, name, sizeof(name_buf));
        if (ret < 0) {
            pr_warn("copy_from_user failed for '%s': %d\n", proc->name, ret);
            return ret;
        }
        name_buf[HANDLE_NAME_MAX - 1] = '\0';
    } else {
        return -EINVAL;
    }

    handle_t h = handle_find(proc, name_buf);
    if (h == HANDLE_INVALID) {
        return -ENOENT;
    }

    return (int32_t)h;
}

void sys_handle_init(void) {
    syscall_register(SYS_HANDLE_CLOSE, sys_handle_close, 1, "handle_close");
    syscall_register(SYS_HANDLE_DUPLICATE, sys_handle_duplicate, 3, "handle_duplicate");
    syscall_register(SYS_HANDLE_GRANT, sys_handle_grant, 3, "handle_grant");
    syscall_register(SYS_HANDLE_FIND, sys_handle_find, 1, "handle_find");
}
