/**
 * @file sys_pipe.c
 * @brief 管道系统调用
 */

#include <ipc/pipe.h>
#include <sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/process.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

/* SYS_PIPE_CREATE: ebx=read_h*, ecx=write_h* */
static int32_t sys_pipe_create(const uint32_t *args) {
    handle_t *user_read_h  = (handle_t *)(uintptr_t)args[0];
    handle_t *user_write_h = (handle_t *)(uintptr_t)args[1];

    if (!user_read_h || !user_write_h) {
        return -EINVAL;
    }

    handle_t rh, wh;
    int ret = pipe_create(&rh, &wh);
    if (ret < 0) {
        return ret;
    }

    ret = copy_to_user(user_read_h, &rh, sizeof(handle_t));
    if (ret < 0) {
        struct process *proc = process_current();
        handle_free(proc, rh);
        handle_free(proc, wh);
        return ret;
    }

    ret = copy_to_user(user_write_h, &wh, sizeof(handle_t));
    if (ret < 0) {
        struct process *proc = process_current();
        handle_free(proc, rh);
        handle_free(proc, wh);
        return ret;
    }

    return 0;
}

/* SYS_PIPE_READ: ebx=handle, ecx=buf, edx=size */
static int32_t sys_pipe_read(const uint32_t *args) {
    handle_t handle = (handle_t)args[0];
    void    *ubuf   = (void *)(uintptr_t)args[1];
    uint32_t size   = args[2];

    struct process     *proc = process_current();
    struct handle_entry entry;

    if (!proc) return -EINVAL;

    if (handle_acquire(proc, handle, HANDLE_PIPE_READ, &entry) < 0) {
        return -EBADF;
    }

    struct ipc_pipe *p = entry.object;
    int ret = pipe_read(p, ubuf, size);

    handle_object_put(entry.type, entry.object);
    return ret;
}

/* SYS_PIPE_WRITE: ebx=handle, ecx=buf, edx=size */
static int32_t sys_pipe_write(const uint32_t *args) {
    handle_t    handle = (handle_t)args[0];
    const void *ubuf   = (const void *)(uintptr_t)args[1];
    uint32_t    size   = args[2];

    struct process     *proc = process_current();
    struct handle_entry entry;

    if (!proc) return -EINVAL;

    if (handle_acquire(proc, handle, HANDLE_PIPE_WRITE, &entry) < 0) {
        return -EBADF;
    }

    struct ipc_pipe *p = entry.object;
    int ret = pipe_write(p, ubuf, size);

    handle_object_put(entry.type, entry.object);
    return ret;
}

void sys_pipe_init(void) {
    syscall_register(SYS_PIPE_CREATE, sys_pipe_create, 2, "pipe_create");
    syscall_register(SYS_PIPE_READ, sys_pipe_read, 3, "pipe_read");
    syscall_register(SYS_PIPE_WRITE, sys_pipe_write, 3, "pipe_write");
}
