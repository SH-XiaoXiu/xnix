/**
 * @file svc_connect.c
 * @brief 服务连接和 IPC 收发 API
 */

#include <errno.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

int svc_connect(const char *service_name) {
    if (!service_name) {
        errno = EINVAL;
        return -1;
    }

    handle_t h = (handle_t)env_get_handle(service_name);
    if (h == HANDLE_INVALID) {
        errno = ENOENT;
        return -1;
    }

    int fd = fd_alloc();
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    if (!fd_install(fd, h, 0, 0, FD_FLAG_READ | FD_FLAG_WRITE)) {
        fd_free(fd);
        errno = EMFILE;
        return -1;
    }

    return fd;
}

int svc_create(const char *service_name) {
    int ep = sys_endpoint_create(service_name);
    if (ep < 0) {
        return -1; /* errno set by sys_endpoint_create */
    }

    int fd = fd_alloc();
    if (fd < 0) {
        sys_handle_close((uint32_t)ep);
        errno = EMFILE;
        return -1;
    }

    if (!fd_install(fd, (handle_t)ep, 0, 0, FD_FLAG_READ | FD_FLAG_WRITE)) {
        sys_handle_close((uint32_t)ep);
        fd_free(fd);
        errno = EMFILE;
        return -1;
    }

    return fd;
}

int svc_send(int fd, const void *msg, size_t len) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }

    struct ipc_message ipc_msg;
    memset(&ipc_msg, 0, sizeof(ipc_msg));
    ipc_msg.buffer.data = (uint64_t)(uintptr_t)msg;
    ipc_msg.buffer.size = (uint32_t)len;

    int ret = sys_ipc_send(ent->handle, &ipc_msg, 5000);
    if (ret < 0) {
        return -1; /* errno set by sys_ipc_send */
    }
    return 0;
}

int svc_recv(int fd, void *msg, size_t len) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }

    struct ipc_message ipc_msg;
    memset(&ipc_msg, 0, sizeof(ipc_msg));
    ipc_msg.buffer.data = (uint64_t)(uintptr_t)msg;
    ipc_msg.buffer.size = (uint32_t)len;

    int ret = sys_ipc_receive(ent->handle, &ipc_msg, 0);
    if (ret < 0) {
        return -1; /* errno set by sys_ipc_receive */
    }
    return (int)ipc_msg.buffer.size;
}
