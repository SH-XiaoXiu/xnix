/**
 * @file io.c
 * @brief 统一 POSIX I/O 函数
 *
 * 提供 read/write/close/dup/dup2/pipe/open,按 fd 类型分发到对应的 IPC 协议.
 */

#include <d/protocol/tty.h>
#include <d/protocol/vfs.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

/* pipe 数据消息 opcode */
#define PIPE_OP_DATA 0xFD01
#define PIPE_OP_EOF  0xFD02

/* ---- write ---- */

static ssize_t write_tty(struct fd_entry *ent, const void *buf, size_t n) {
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = TTY_OP_WRITE;
    msg.regs.data[1] = (uint32_t)n;
    msg.buffer.data  = (uint64_t)(uintptr_t)buf;
    msg.buffer.size  = (uint32_t)n;

    int ret = sys_ipc_send(ent->handle, &msg, 100);
    if (ret < 0) {
        return -EIO;
    }
    return (ssize_t)n;
}

static ssize_t write_vfs(struct fd_entry *ent, const void *buf, size_t n) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_WRITE;
    msg.regs.data[1] = ent->vfs.fs_handle;
    msg.regs.data[2] = ent->vfs.offset;
    msg.regs.data[3] = (uint32_t)n;
    msg.buffer.data  = (uint64_t)(uintptr_t)buf;
    msg.buffer.size  = (uint32_t)n;

    int ret = sys_ipc_call(ent->vfs.fs_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result > 0) {
        ent->vfs.offset += (uint32_t)result;
    }
    return result;
}

static ssize_t write_pipe(struct fd_entry *ent, const void *buf, size_t n) {
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = PIPE_OP_DATA;
    msg.regs.data[1] = (uint32_t)n;
    msg.buffer.data  = (uint64_t)(uintptr_t)buf;
    msg.buffer.size  = (uint32_t)n;

    int ret = sys_ipc_send(ent->handle, &msg, 5000);
    if (ret < 0) {
        return -EIO;
    }
    return (ssize_t)n;
}

ssize_t write(int fd, const void *buf, size_t n) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        return -EBADF;
    }
    if (!buf || n == 0) {
        return 0;
    }

    switch (ent->type) {
    case FD_TYPE_TTY:
        return write_tty(ent, buf, n);
    case FD_TYPE_VFS:
        return write_vfs(ent, buf, n);
    case FD_TYPE_PIPE:
        return write_pipe(ent, buf, n);
    default:
        return -EBADF;
    }
}

/* ---- read ---- */

static ssize_t read_tty(struct fd_entry *ent, void *buf, size_t n) {
    struct ipc_message req;
    struct ipc_message reply;
    char               recv_buf[64];

    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = TTY_OP_READ;
    req.regs.data[1] = (uint32_t)n;

    size_t recv_size  = (n < sizeof(recv_buf)) ? n : sizeof(recv_buf);
    reply.buffer.data = (uint64_t)(uintptr_t)recv_buf;
    reply.buffer.size = (uint32_t)recv_size;

    int ret = sys_ipc_call(ent->handle, &req, &reply, 0);
    if (ret < 0) {
        return -EIO;
    }

    int count = (int)reply.regs.data[0];
    if (count <= 0) {
        return count;
    }
    if ((size_t)count > n) {
        count = (int)n;
    }
    memcpy(buf, recv_buf, (size_t)count);
    return count;
}

static ssize_t read_vfs(struct fd_entry *ent, void *buf, size_t n) {
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_READ;
    msg.regs.data[1] = ent->vfs.fs_handle;
    msg.regs.data[2] = ent->vfs.offset;
    msg.regs.data[3] = (uint32_t)n;

    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = (uint32_t)n;

    int ret = sys_ipc_call(ent->vfs.fs_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result > 0) {
        ent->vfs.offset += (uint32_t)result;
    }
    return result;
}

static ssize_t read_pipe(struct fd_entry *ent, void *buf, size_t n) {
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));

    msg.buffer.data = (uint64_t)(uintptr_t)buf;
    msg.buffer.size = (uint32_t)n;

    int ret = sys_ipc_receive(ent->handle, &msg, 0);
    if (ret < 0) {
        return -EIO;
    }

    uint32_t op = msg.regs.data[0];
    if (op == PIPE_OP_EOF) {
        return 0;
    }

    return (ssize_t)msg.regs.data[1];
}

ssize_t read(int fd, void *buf, size_t n) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        return -EBADF;
    }
    if (!buf || n == 0) {
        return 0;
    }

    switch (ent->type) {
    case FD_TYPE_TTY:
        return read_tty(ent, buf, n);
    case FD_TYPE_VFS:
        return read_vfs(ent, buf, n);
    case FD_TYPE_PIPE:
        return read_pipe(ent, buf, n);
    default:
        return -EBADF;
    }
}

/* ---- close ---- */

int close(int fd) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        return -EBADF;
    }

    /* VFS 文件需要通知 FS 驱动 */
    if (ent->type == FD_TYPE_VFS) {
        struct ipc_message msg   = {0};
        struct ipc_message reply = {0};
        msg.regs.data[0]         = UDM_VFS_CLOSE;
        msg.regs.data[1]         = ent->vfs.fs_handle;
        sys_ipc_call(ent->vfs.fs_ep, &msg, &reply, 1000);
    }

    sys_handle_close(ent->handle);
    fd_free(fd);
    return 0;
}

/* ---- dup ---- */

int dup(int oldfd) {
    struct fd_entry *ent = fd_get(oldfd);
    if (!ent) {
        return -EBADF;
    }

    int new_handle = sys_handle_duplicate(ent->handle, HANDLE_INVALID, NULL);
    if (new_handle < 0) {
        return -EMFILE;
    }

    int newfd = fd_alloc();
    if (newfd < 0) {
        sys_handle_close((uint32_t)new_handle);
        return -EMFILE;
    }

    struct fd_entry *dst = fd_install(newfd, (handle_t)new_handle, ent->type, ent->flags);
    if (!dst) {
        sys_handle_close((uint32_t)new_handle);
        return -EMFILE;
    }

    /* 复制 VFS 状态 */
    if (ent->type == FD_TYPE_VFS) {
        dst->vfs = ent->vfs;
    }

    return newfd;
}

/* ---- dup2 ---- */

int dup2(int oldfd, int newfd) {
    if (oldfd == newfd) {
        /* POSIX: 如果 oldfd 有效, 返回 newfd */
        if (!fd_get(oldfd)) {
            return -EBADF;
        }
        return newfd;
    }

    struct fd_entry *ent = fd_get(oldfd);
    if (!ent) {
        return -EBADF;
    }

    if (newfd < 0 || newfd >= FD_MAX) {
        return -EBADF;
    }

    /* 如果 newfd 已打开,先关闭 */
    if (fd_get(newfd)) {
        close(newfd);
    }

    int new_handle = sys_handle_duplicate(ent->handle, (handle_t)newfd, NULL);
    if (new_handle < 0) {
        return -EMFILE;
    }

    struct fd_entry *dst = fd_install(newfd, (handle_t)new_handle, ent->type, ent->flags);
    if (!dst) {
        sys_handle_close((uint32_t)new_handle);
        return -EMFILE;
    }

    /* 复制 VFS 状态 */
    if (ent->type == FD_TYPE_VFS) {
        dst->vfs = ent->vfs;
    }

    return newfd;
}

/* ---- pipe ---- */

int pipe(int fds[2]) {
    int ep = sys_endpoint_create(NULL);
    if (ep < 0) {
        return -EMFILE;
    }

    int dup_ep = sys_handle_duplicate((uint32_t)ep, HANDLE_INVALID, NULL);
    if (dup_ep < 0) {
        sys_handle_close((uint32_t)ep);
        return -EMFILE;
    }

    /* 读端 */
    int rfd = fd_alloc();
    if (rfd < 0) {
        sys_handle_close((uint32_t)ep);
        sys_handle_close((uint32_t)dup_ep);
        return -EMFILE;
    }
    fd_install(rfd, (handle_t)ep, FD_TYPE_PIPE, FD_FLAG_READ);

    /* 写端 */
    int wfd = fd_alloc();
    if (wfd < 0) {
        close(rfd);
        sys_handle_close((uint32_t)dup_ep);
        return -EMFILE;
    }
    fd_install(wfd, (handle_t)dup_ep, FD_TYPE_PIPE, FD_FLAG_WRITE);

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

/* ---- open ---- */

/* vfsd endpoint (延迟初始化) */
static uint32_t g_io_vfsd_ep = HANDLE_INVALID;

static int io_ensure_vfsd(void) {
    if (g_io_vfsd_ep != HANDLE_INVALID) {
        return 0;
    }
    uint32_t h = env_get_handle("vfs_ep");
    if (h == HANDLE_INVALID) {
        return -ENOENT;
    }
    g_io_vfsd_ep = h;
    return 0;
}

int open(const char *path, int flags) {
    if (!path) {
        return -EINVAL;
    }

    int ret = io_ensure_vfsd();
    if (ret < 0) {
        return ret;
    }

    int fd = fd_alloc();
    if (fd < 0) {
        return -EMFILE;
    }

    /* IPC 到 vfsd (UDM_VFS_OPEN) */
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_OPEN;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.regs.data[2] = (uint32_t)flags;
    msg.buffer.data  = (uint64_t)(uintptr_t)path;
    msg.buffer.size  = strlen(path);

    ret = sys_ipc_call(g_io_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    uint8_t fd_flags = 0;
    if ((flags & 0x03) == O_RDONLY) {
        fd_flags = FD_FLAG_READ;
    } else if ((flags & 0x03) == O_WRONLY) {
        fd_flags = FD_FLAG_WRITE;
    } else {
        fd_flags = FD_FLAG_READ | FD_FLAG_WRITE;
    }

    struct fd_entry *ent = fd_install(fd, HANDLE_INVALID, FD_TYPE_VFS, fd_flags);
    if (!ent) {
        return -EMFILE;
    }

    ent->vfs.fs_handle = (uint32_t)result;
    if (reply.handles.count > 0) {
        ent->vfs.fs_ep = reply.handles.handles[0];
    }
    ent->vfs.offset = 0;
    ent->vfs.flags  = (uint32_t)flags;

    return fd;
}
