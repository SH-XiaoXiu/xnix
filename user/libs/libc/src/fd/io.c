/**
 * @file io.c
 * @brief 统一 POSIX I/O 函数
 *
 * 所有 fd 统一走 IO_READ/IO_WRITE 协议.
 * pipe 也走 IO 协议 — pipe server thread 在对端处理.
 * 进程退出前 libc 会统一 close 所有 fd, 因此 pipe close 同样发送 IO_CLOSE。
 */

#include <stdio_internal.h>
#include <xnix/abi/io.h>
#include <xnix/protocol/devfs.h>
#include <xnix/protocol/vfs.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

/* ---- write ---- */

static ssize_t write_io(int fd, struct fd_entry *ent, const void *buf, size_t n) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_WRITE;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = ent->offset;
    msg.regs.data[3] = (uint32_t)n;
    msg.buffer.data  = (uint64_t)(uintptr_t)buf;
    msg.buffer.size  = (uint32_t)n;

    int ret = sys_ipc_call(ent->handle, &msg, &reply, 5000);
    if (ret < 0) {
        (void)fd;
        return -1;
    }

    int32_t written = (int32_t)reply.regs.data[0];
    if (written < 0) {
        errno = -written;
        return -1;
    }
    if (written > 0) ent->offset += (uint32_t)written;
    return (ssize_t)written;
}

ssize_t write(int fd, const void *buf, size_t n) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }
    if (!buf || n == 0) {
        return 0;
    }

    if (ent->flags & FD_FLAG_PIPE) {
        return (ssize_t)sys_pipe_write(ent->handle, buf, (uint32_t)n);
    }

    return write_io(fd, ent, buf, n);
}

/* ---- read ---- */

static ssize_t read_io(int fd, struct fd_entry *ent, void *buf, size_t n) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_READ;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = ent->offset;
    msg.regs.data[3] = (uint32_t)n;

    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = (uint32_t)n;

    int ret = sys_ipc_call(ent->handle, &msg, &reply, 30000);
    if (ret < 0) {
        (void)fd;
        return -1;
    }

    int32_t nread = (int32_t)reply.regs.data[0];
    if (nread < 0) {
        errno = -nread;
        return -1;
    }
    if (nread > 0) {
        ent->offset += (uint32_t)nread;
    }
    return (ssize_t)nread;
}

ssize_t read(int fd, void *buf, size_t n) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }
    if (!buf || n == 0) {
        return 0;
    }

    if (ent->flags & FD_FLAG_PIPE) {
        return (ssize_t)sys_pipe_read(ent->handle, buf, (uint32_t)n);
    }

    return read_io(fd, ent, buf, n);
}

/* ---- close ---- */

int close(int fd) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }

    if (ent->flags & FD_FLAG_PIPE) {
        /* Pipe: 内核 handle close 自动触发 EOF/EPIPE, 不需要发 IO_CLOSE */
        sys_handle_close(ent->handle);
        fd_free(fd);
        return 0;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = IO_CLOSE;
    msg.regs.data[1] = ent->session;
    sys_ipc_call(ent->handle, &msg, &reply, 1000);

    sys_handle_close(ent->handle);
    fd_free(fd);
    return 0;
}

/* ---- dup ---- */

int dup(int oldfd) {
    struct fd_entry *ent = fd_get(oldfd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }

    int new_handle = sys_handle_duplicate(ent->handle, HANDLE_INVALID, NULL);
    if (new_handle < 0) {
        errno = EMFILE;
        return -1;
    }

    int newfd = fd_alloc();
    if (newfd < 0) {
        sys_handle_close((uint32_t)new_handle);
        errno = EMFILE;
        return -1;
    }

    fd_install(newfd, (handle_t)new_handle, ent->session, ent->offset, ent->flags);
    return newfd;
}

/* ---- dup2 ---- */

int dup2(int oldfd, int newfd) {
    if (oldfd == newfd) {
        if (!fd_get(oldfd)) {
            errno = EBADF;
            return -1;
        }
        return newfd;
    }

    struct fd_entry *ent = fd_get(oldfd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }

    if (newfd < 0 || newfd >= FD_MAX) {
        errno = EBADF;
        return -1;
    }

    if (fd_get(newfd)) {
        close(newfd);
    }

    int new_handle = sys_handle_duplicate(ent->handle, (handle_t)newfd, NULL);
    if (new_handle < 0) {
        errno = EMFILE;
        return -1;
    }

    fd_install(newfd, (handle_t)new_handle, ent->session, ent->offset, ent->flags);
    return newfd;
}

/* ---- pipe ---- */

int pipe(int fds[2]) {
    handle_t rh, wh;
    int ret = sys_pipe_create(&rh, &wh);
    if (ret < 0) {
        return -1;
    }

    int rfd = fd_alloc();
    if (rfd < 0) {
        sys_handle_close(rh);
        sys_handle_close(wh);
        errno = EMFILE;
        return -1;
    }
    fd_install(rfd, rh, 0, 0, FD_FLAG_READ | FD_FLAG_PIPE);

    int wfd = fd_alloc();
    if (wfd < 0) {
        close(rfd);
        sys_handle_close(wh);
        errno = EMFILE;
        return -1;
    }
    fd_install(wfd, wh, 0, 0, FD_FLAG_WRITE | FD_FLAG_PIPE);

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

/* ---- open ---- */

static uint32_t g_io_vfsd_ep = HANDLE_INVALID;

static int io_ensure_vfsd(void) {
    if (g_io_vfsd_ep != HANDLE_INVALID) {
        return 0;
    }
    uint32_t h = env_get_handle("vfs_ep");
    if (h == HANDLE_INVALID) {
        errno = ENOENT;
        return -1;
    }
    g_io_vfsd_ep = h;
    return 0;
}

int open(const char *path, int flags) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    int ret = io_ensure_vfsd();
    if (ret < 0) {
        return -1;
    }

    int fd = fd_alloc();
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_OPEN;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.regs.data[2] = (uint32_t)flags;
    msg.buffer.data  = (uint64_t)(uintptr_t)path;
    msg.buffer.size  = strlen(path);

    ret = sys_ipc_call(g_io_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        fd_free(fd);
        errno = EIO;
        return -1;
    }

    int32_t result = (int32_t)reply.regs.data[0];
    if (result < 0) {
        fd_free(fd);
        errno = -result;
        return -1;
    }

    uint8_t fd_flags = 0;
    if ((flags & 0x03) == O_RDONLY) {
        fd_flags = FD_FLAG_READ;
    } else if ((flags & 0x03) == O_WRONLY) {
        fd_flags = FD_FLAG_WRITE;
    } else {
        fd_flags = FD_FLAG_READ | FD_FLAG_WRITE;
    }

    /* io_ep: 服务端返回的直接 endpoint (用于后续 IO_READ/IO_WRITE) */
    handle_t io_ep = HANDLE_INVALID;
    if (reply.handles.count > 0) {
        io_ep = reply.handles.handles[0];
    }

    /* TTY 设备直接返回可用 IO endpoint; session=0 仅表示该对象无需额外会话号 */
    uint32_t dev_type = reply.regs.data[2];
    if (dev_type == DEVFS_TYPE_TTY && io_ep != HANDLE_INVALID) {
        fd_install(fd, io_ep, 0, 0, fd_flags);
        return fd;
    }

    /* 普通 VFS 文件: session=fs_handle, handle=fs_ep */
    uint32_t session = (uint32_t)result;
    if (io_ep == HANDLE_INVALID) {
        io_ep = g_io_vfsd_ep;
    }
    fd_install(fd, io_ep, session, 0, fd_flags);
    return fd;
}
