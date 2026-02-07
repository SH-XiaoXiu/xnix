/**
 * @file vfs_client.c
 * @brief 用户态 VFS 客户端实现(微内核架构)
 *
 * 通过 vfsd 服务器进行路径解析和挂载表管理.
 */

#include <d/protocol/vfs.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/errno.h>
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

/* 文件描述符表 */
#define VFS_MAX_FD 32

struct vfs_file {
    uint32_t fs_handle; /* FS driver 内部的 handle */
    uint32_t fs_ep;     /* FS driver endpoint */
    uint32_t offset;
    uint32_t flags;
    int      in_use;
};

static struct vfs_file fd_table[VFS_MAX_FD];

/* resolve_path 已移至 vfsd,客户端直接传递路径给 vfsd */

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
    for (int i = 0; i < VFS_MAX_FD; i++) {
        fd_table[i].in_use = 0;
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
 * 分配文件描述符
 */
static int vfs_alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].in_use = 1;
            fd_table[i].offset = 0;
            return i;
        }
    }
    return -EMFILE;
}

/**
 * 打开文件(通过 vfsd)
 */
int vfs_open(const char *path, uint32_t flags) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    int fd = vfs_alloc_fd();
    if (fd < 0) {
        return fd;
    }

    /* 构造 IPC 消息 */
    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_OPEN;
    msg.regs.data[1] = (uint32_t)sys_getpid(); /* 传递 PID 供 vfsd 解析路径 */
    msg.regs.data[2] = flags;
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    /* 发送给 vfsd,vfsd 会转发给对应的 FS 驱动 */
    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        fd_table[fd].in_use = 0;
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        fd_table[fd].in_use = 0;
        return result;
    }

    /* vfsd 在 reply.handles.handles[0] 返回 fs_ep */
    fd_table[fd].fs_handle = (uint32_t)result;
    if (reply.handles.count > 0) {
        fd_table[fd].fs_ep = reply.handles.handles[0];
    }
    fd_table[fd].flags = flags;

    return fd;
}

/**
 * 关闭文件(直接与 FS 驱动通信)
 */
int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
        return -EBADF;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_CLOSE;
    msg.regs.data[1] = fd_table[fd].fs_handle;

    sys_ipc_call(fd_table[fd].fs_ep, &msg, &reply, 1000);

    fd_table[fd].in_use = 0;
    return 0;
}

/**
 * 读取文件(直接与 FS 驱动通信)
 */
ssize_t vfs_read(int fd, void *buf, size_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
        return -EBADF;
    }

    if (!buf || size == 0) {
        return 0;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_READ;
    msg.regs.data[1] = fd_table[fd].fs_handle;
    msg.regs.data[2] = fd_table[fd].offset;
    msg.regs.data[3] = size;

    /* 预设接收 buffer(零拷贝)*/
    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = (uint32_t)size;

    int ret = sys_ipc_call(fd_table[fd].fs_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    /* 数据已经被内核直接复制到 buf 中 */
    if (result > 0) {
        fd_table[fd].offset += (uint32_t)result;
    }
    return result;
}

/**
 * 写入文件(直接与 FS 驱动通信)
 */
ssize_t vfs_write(int fd, const void *buf, size_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
        return -EBADF;
    }

    if (!buf || size == 0) {
        return 0;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_WRITE;
    msg.regs.data[1] = fd_table[fd].fs_handle;
    msg.regs.data[2] = fd_table[fd].offset;
    msg.regs.data[3] = size;
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)buf;
    msg.buffer.size  = size;

    int ret = sys_ipc_call(fd_table[fd].fs_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result > 0) {
        fd_table[fd].offset += result;
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
 * 打开目录(通过 vfsd)
 */
int vfs_opendir(const char *path) {
    if (!path) {
        return -EINVAL;
    }
    int init_ret = vfs_ensure_vfsd();
    if (init_ret < 0) {
        return init_ret;
    }

    int fd = vfs_alloc_fd();
    if (fd < 0) {
        return fd;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_OPENDIR;
    msg.regs.data[1] = (uint32_t)sys_getpid();
    msg.buffer.data  = (uint64_t)(uintptr_t)(void *)path;
    msg.buffer.size  = strlen(path);

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        fd_table[fd].in_use = 0;
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        fd_table[fd].in_use = 0;
        return result;
    }

    fd_table[fd].fs_handle = (uint32_t)result;
    fd_table[fd].fs_ep     = reply.handles.handles[0];
    fd_table[fd].flags     = 0;
    return fd;
}

/**
 * 读取目录项(直接与 FS 驱动通信)
 */
int vfs_readdir(int fd, char *name, size_t size) {
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
        return -EBADF;
    }

    if (!name || size == 0) {
        return -EINVAL;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    char               tmp_name[256];

    msg.regs.data[0] = UDM_VFS_READDIR;
    msg.regs.data[1] = fd_table[fd].fs_handle;
    msg.regs.data[2] = fd_table[fd].offset;

    /* 预设接收 buffer */
    reply.buffer.data = (uint64_t)(uintptr_t)tmp_name;
    reply.buffer.size = sizeof(tmp_name);

    int ret = sys_ipc_call(fd_table[fd].fs_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result <= 0) {
        return result;
    }

    /* 数据已经在 tmp_name 中 */
    size_t copy_size = strlen(tmp_name);
    if (copy_size > size - 1) {
        copy_size = size - 1;
    }
    memcpy(name, tmp_name, copy_size);
    name[copy_size] = '\0';
    fd_table[fd].offset++;
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

    /* vfsd 已经将路径写入 buf,确保 null 终止 */
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
    msg.regs.data[1] = (uint32_t)sys_getpid(); /* 父进程 PID */
    msg.regs.data[2] = (uint32_t)child_pid;    /* 子进程 PID */

    int ret = sys_ipc_call(g_vfsd_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    return (int32_t)reply.regs.data[1];
}

/**
 * 读取目录项(带索引) (直接与 FS 驱动通信)
 */
int vfs_readdir_index(int fd, uint32_t index, struct vfs_dirent *dirent) {
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
        return -EBADF;
    }

    if (!dirent) {
        return -EINVAL;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = UDM_VFS_READDIR;
    msg.regs.data[1] = fd_table[fd].fs_handle;
    msg.regs.data[2] = index;

    /* 预设接收 buffer */
    reply.buffer.data = (uint64_t)(uintptr_t)dirent;
    reply.buffer.size = sizeof(*dirent);

    int ret = sys_ipc_call(fd_table[fd].fs_ep, &msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    /* 数据已经被内核复制到 dirent 中 */
    return 0;
}
