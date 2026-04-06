/**
 * @file vfs_client.c
 * @brief 用户态 VFS 客户端实现(微内核架构)
 *
 * 通过 vfsd 服务器进行路径解析和挂载表管理.
 * 文件操作使用统一 fd 表.
 */

#include <xnix/abi/io.h>
#include <xnix/protocol/devfs.h>
#include <xnix/protocol/vfs.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/ipc/fs.h>
#include <xnix/syscall.h>

/* VFS 服务器 endpoint */
static uint32_t g_vfsd_ep = HANDLE_INVALID;

static int vfs_ensure_vfsd(void) {
    if (g_vfsd_ep != HANDLE_INVALID) {
        return 0;
    }
    uint32_t h = env_get_handle("vfs_ep");
    if (h == HANDLE_INVALID) {
        return -ENOENT;
    }
    g_vfsd_ep = h;
    return 0;
}

/**
 * 初始化 VFS 客户端
 */
void vfs_client_init(uint32_t vfsd_ep) {
    if (vfsd_ep == HANDLE_INVALID) {
        vfsd_ep = env_get_handle("vfs_ep");
    }
    if (vfsd_ep != HANDLE_INVALID) {
        g_vfsd_ep = vfsd_ep;
    }
}

/**
 * 挂载文件系统(通过 vfsd,传递 handle)
 */
int vfs_mount(const char *path, uint32_t fs_ep) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = 0x1000; /* VFS_MOUNT */
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    /* 通过 IPC handle 传递 FS endpoint */
    msg.handles.handles[0] = fs_ep;
    msg.handles.count      = 1;

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    return (int32_t)reply.regs.data[1];
}

/**
 * 打开文件(通过 vfsd) - 使用统一 fd 表
 */
int vfs_open(const char *path, uint32_t flags) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    int fd = fd_alloc();
    if (fd < 0) {
        return -EMFILE;
    }

    /* 构造 IPC 消息 */
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_OPEN;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.regs.data[2] = flags;
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    /* 设备类型检查: devfsd 对 TTY 设备返回 DEVFS_TYPE_TTY + endpoint handle */
    uint32_t dev_type = reply.regs.data[2];
    if (dev_type == DEVFS_TYPE_TTY && reply.handles.count > 0) {
        fd_install(fd, reply.handles.handles[0], 0, 0,
                   FD_FLAG_READ | FD_FLAG_WRITE);
        return fd;
    }

    handle_t fs_ep = HANDLE_INVALID;
    if (reply.handles.count > 0) {
        fs_ep = reply.handles.handles[0];
    }

    struct fd_entry *ent =
        fd_install(fd, fs_ep, (uint32_t)result, 0, FD_FLAG_READ | FD_FLAG_WRITE);
    if (!ent) {
        return -EMFILE;
    }

    return fd;
}

/**
 * 关闭文件(直接与 FS 驱动通信) - 使用统一 fd 表
 */
int vfs_close(int fd) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        return -EBADF;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    if (ent->handle == g_vfsd_ep) {
        msg.regs.data[0] = UDM_VFS_CLOSE;
        msg.regs.data[1] = ent->session;
    } else {
        msg.regs.data[0] = IO_CLOSE;
        msg.regs.data[1] = ent->session;
    }

    sys_ipc_call(ent->handle, &msg, &reply, 1000);

    fd_free(fd);
    return 0;
}

/**
 * 读取文件(直接与 FS 驱动通信) - 使用统一 IO 协议
 */
ssize_t vfs_read(int fd, void *buf, size_t size) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        return -EBADF;
    }

    if (!buf || size == 0) {
        return 0;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_READ;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = ent->offset;
    msg.regs.data[3] = size;

    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = (uint32_t)size;

    /* stream (session==0): infinite wait for input; file: 5s timeout */
    uint32_t timeout = (ent->session == 0) ? 0 : 5000;

    int ret = sys_ipc_call(ent->handle, &msg, &reply, timeout);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[0];
    if (result < 0) {
        return result;
    }

    if (result > 0) {
        ent->offset += (uint32_t)result;
    }
    return result;
}

/**
 * 写入文件(直接与 FS 驱动通信) - 使用统一 IO 协议
 */
ssize_t vfs_write(int fd, const void *buf, size_t size) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        return -EBADF;
    }

    if (!buf || size == 0) {
        return 0;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_WRITE;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = ent->offset;
    msg.regs.data[3] = size;
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)buf;
    msg.buffer.size  = size;

    int ret = sys_ipc_call(ent->handle, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[0];
    if (result > 0) {
        ent->offset += result;
    }

    return result;
}

/**
 * 创建目录(通过 vfsd)
 */
int vfs_mkdir(const char *path) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_MKDIR;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    return (int32_t)reply.regs.data[1];
}

/**
 * 删除文件或目录(通过 vfsd)
 */
int vfs_delete(const char *path) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_DEL;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    return (int32_t)reply.regs.data[1];
}

/**
 * 获取文件信息(通过 vfsd)
 */
int vfs_stat(const char *path, struct vfs_stat *st) {
    if (!st || !path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_INFO;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    st->size = reply.regs.data[2];
    st->type = reply.regs.data[3];

    return 0;
}

/**
 * 打开目录(通过 vfsd) - 使用统一 fd 表
 */
int vfs_opendir(const char *path) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    int fd = fd_alloc();
    if (fd < 0) {
        return -EMFILE;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_OPENDIR;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    handle_t dir_ep = HANDLE_INVALID;
    if (reply.handles.count > 0) {
        dir_ep = reply.handles.handles[0];
    }

    struct fd_entry *ent =
        fd_install(fd, dir_ep, (uint32_t)result, 0, FD_FLAG_READ);
    if (!ent) {
        return -EMFILE;
    }

    return fd;
}

/**
 * 读取目录项(直接与 FS 驱动通信) - 使用统一 fd 表
 */
int vfs_readdir(int fd, char *name, size_t size) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent || ent->session == 0) {
        return -EBADF;
    }

    if (!name || size == 0) {
        return -EINVAL;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    char               tmp_name[256];

    msg.regs.data[0] = UDM_VFS_READDIR;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = ent->offset;

    reply.buffer.data = (uint64_t)(uintptr_t)tmp_name;
    reply.buffer.size = sizeof(tmp_name);

    int ret = sys_ipc_call(ent->handle, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result <= 0) {
        return result;
    }

    size_t copy_size = strlen(tmp_name);
    if (copy_size > size - 1) {
        copy_size = size - 1;
    }
    memcpy(name, tmp_name, copy_size);
    name[copy_size] = '\0';
    ent->offset++;
    return 1;
}

/**
 * 改变当前工作目录(通过 vfsd)
 */
int vfs_chdir(const char *path) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_CHDIR;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    return (int32_t)reply.regs.data[1];
}

/**
 * 获取当前工作目录(通过 vfsd)
 */
int vfs_getcwd(char *buf, size_t size) {
    if (!buf || size == 0) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_GETCWD;
    msg.regs.data[1] = (uint32_t)sys_getpid();

    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = size;

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    if (reply.buffer.size < size) {
        buf[reply.buffer.size] = '\0';
    } else if (size > 0) {
        buf[size - 1] = '\0';
    }

    return 0;
}

/**
 * 复制当前进程的CWD到子进程(用于进程spawn后的CWD继承)
 */
int vfs_copy_cwd_to_child(pid_t child_pid) {
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_COPY_CWD;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.regs.data[2] = (uint32_t)child_pid;

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    return (int32_t)reply.regs.data[1];
}

/**
 * 读取目录项(带索引) - 使用统一 fd 表
 */
int vfs_readdir_index(int fd, uint32_t index, struct vfs_dirent *dirent) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent || ent->session == 0) {
        return -EBADF;
    }

    if (!dirent) {
        return -EINVAL;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_READDIR;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = index;

    reply.buffer.data = (uint64_t)(uintptr_t)dirent;
    reply.buffer.size = sizeof(*dirent);

    int ret = sys_ipc_call(ent->handle, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    return 0;
}
