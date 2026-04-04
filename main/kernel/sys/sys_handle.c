#include <arch/cpu.h>

#include <sys/syscall.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/syscall.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/cap.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
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

    /* 检查源 handle 有 DUPLICATE 权限 */
    struct handle_entry src_entry;
    if (handle_acquire(proc, src_h, HANDLE_NONE, &src_entry) < 0) {
        return -EINVAL;
    }
    handle_object_put(src_entry.type, src_entry.object);

    if (!(src_entry.rights & HANDLE_RIGHT_DUPLICATE)) {
        return -EPERM;
    }

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

    handle_t dst_h = handle_transfer(proc, src_h, proc, final_name, dst_hint, 0);
    if (dst_h == HANDLE_INVALID) {
        return -EINVAL;
    }

    return (int32_t)dst_h;
}

/* SYS_HANDLE_GRANT: ebx=pid, ecx=src_handle, edx=name, esi=rights */
static int32_t sys_handle_grant(const uint32_t *args) {
    pid_t           pid     = (pid_t)args[0];
    handle_t        src_h   = (handle_t)args[1];
    const char     *name    = (const char *)(uintptr_t)args[2];
    uint32_t        rights  = args[3];
    struct process *src     = (struct process *)process_current();

    if (!cap_check(src, CAP_HANDLE_GRANT)) {
        return -EPERM;
    }

    /* 检查源 handle 有 TRANSFER 权限 */
    struct handle_entry src_entry;
    if (handle_acquire(src, src_h, HANDLE_NONE, &src_entry) < 0) {
        return -EINVAL;
    }
    handle_object_put(src_entry.type, src_entry.object);

    if (!(src_entry.rights & HANDLE_RIGHT_TRANSFER)) {
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

    handle_t dst_h = handle_transfer(src, src_h, dst, final_name, HANDLE_INVALID, rights);
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
    if (!name) {
        return -EINVAL;
    }
    /* 逐字节拷贝直到 '\0' 或达到上限,避免跨页越界 */
    for (size_t i = 0; i < HANDLE_NAME_MAX; i++) {
        char ch;
        int  ret = copy_from_user(&ch, name + i, 1);
        if (ret < 0) {
            return ret;
        }
        name_buf[i] = ch;
        if (ch == '\0') {
            break;
        }
    }
    name_buf[HANDLE_NAME_MAX - 1] = '\0';

    handle_t h = handle_find(proc, name_buf);
    if (h == HANDLE_INVALID) {
        return -ENOENT;
    }

    return (int32_t)h;
}

/* SYS_HANDLE_LIST: ebx=buf, ecx=max_count, 返回实际写入条数 */
static int32_t sys_handle_list(const uint32_t *args) {
    struct abi_handle_info *user_buf  = (struct abi_handle_info *)(uintptr_t)args[0];
    uint32_t                max_count = args[1];
    struct process         *proc      = (struct process *)process_current();

    if (!user_buf || max_count == 0) {
        return -EINVAL;
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    uint32_t written = 0;
    for (uint32_t i = 0; i < table->capacity && written < max_count; i++) {
        struct handle_entry *entry = &table->entries[i];
        if (entry->type == HANDLE_NONE) {
            continue;
        }

        struct abi_handle_info info;
        info.handle = (handle_t)i;
        info.type   = entry->type;
        info.rights = entry->rights;
        strncpy(info.name, entry->name, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';

        spin_unlock(&table->lock);
        int ret = copy_to_user(&user_buf[written], &info, sizeof(info));
        spin_lock(&table->lock);

        if (ret < 0) {
            spin_unlock(&table->lock);
            return ret;
        }
        written++;
    }

    spin_unlock(&table->lock);
    return (int32_t)written;
}

void sys_handle_init(void) {
    syscall_register(SYS_HANDLE_CLOSE, sys_handle_close, 1, "handle_close");
    syscall_register(SYS_HANDLE_DUPLICATE, sys_handle_duplicate, 3, "handle_duplicate");
    syscall_register(SYS_HANDLE_GRANT, sys_handle_grant, 4, "handle_grant");
    syscall_register(SYS_HANDLE_FIND, sys_handle_find, 1, "handle_find");
    syscall_register(SYS_HANDLE_LIST, sys_handle_list, 2, "handle_list");
}
