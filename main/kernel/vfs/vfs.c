/**
 * @file vfs.c
 * @brief VFS 核心逻辑
 *
 * 通过 IPC 与用户态 fs 服务通信,实现文件操作
 */

#include <kernel/ipc/endpoint.h>
#include <kernel/process/process.h>
#include <kernel/vfs/vfs.h>
#include <xnix/errno.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/udm/vfs.h>

/* mount.c 中的初始化函数 */
extern void vfs_mount_init(void);

void vfs_init(void) {
    vfs_mount_init();
    kprintf("VFS: initialized\n");
}

/**
 * 获取当前进程的 fd 表
 */
static struct fd_table *get_current_fd_table(void) {
    struct process *proc = process_get_current();
    if (!proc) {
        return NULL;
    }
    return proc->fd_table;
}

/**
 * 发送 VFS 请求到 fs 服务并等待回复
 */
static int vfs_ipc_call(struct ipc_endpoint *fs_ep, struct ipc_message *req,
                        struct ipc_message *reply) {
    int ret = ipc_call_direct(fs_ep, req, reply, 5000); /* 5 秒超时 */
    if (ret < 0) {
        return ret;
    }
    return (int32_t)reply->regs.data[1]; /* 回复的结果在 data[1] */
}

int vfs_open(const char *path, uint32_t flags) {
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    /* 查找挂载点 */
    const char       *rel_path;
    struct vfs_mount *mount = vfs_lookup_mount(path, &rel_path);
    if (!mount) {
        return -ENOENT;
    }

    /* 分配 fd */
    int fd = fd_alloc(fdt);
    if (fd < 0) {
        return fd;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        fd_free(fdt, fd);
        return -EINVAL;
    }

    /* 准备 IPC 消息 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_OPEN;
    req.regs.data[1] = flags;
    req.buffer.data  = (void *)rel_path;
    req.buffer.size  = 0;
    while (rel_path[req.buffer.size]) {
        req.buffer.size++;
    }
    req.buffer.size++; /* 包含 null terminator */

    int ret = vfs_ipc_call(mount->fs_ep, &req, &reply);
    if (ret < 0) {
        fd_free(fdt, fd);
        return ret;
    }

    /* 填充 vfs_file */
    file->fs_handle = (uint32_t)ret;
    file->fs_ep     = mount->fs_ep;
    file->offset    = 0;
    file->flags     = flags;

    return fd;
}

int vfs_close(int fd) {
    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        return -EBADF;
    }

    /* 通知 fs 服务关闭 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_CLOSE;
    req.regs.data[1] = file->fs_handle;

    vfs_ipc_call(file->fs_ep, &req, &reply);

    /* 释放 fd */
    fd_free(fdt, fd);
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t size) {
    if (!buf || size == 0) {
        return -EINVAL;
    }

    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        return -EBADF;
    }

    if (!(file->flags & VFS_O_RDONLY)) {
        return -EACCES;
    }

    /* 准备 IPC 消息 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_READ;
    req.regs.data[1] = file->fs_handle;
    req.regs.data[2] = file->offset;
    req.regs.data[3] = (uint32_t)size;
    req.buffer.data  = buf;
    req.buffer.size  = size;

    reply.buffer.data = buf;
    reply.buffer.size = size;

    int ret = vfs_ipc_call(file->fs_ep, &req, &reply);
    if (ret > 0) {
        file->offset += ret;
    }

    return ret;
}

ssize_t vfs_write(int fd, const void *buf, size_t size) {
    if (!buf || size == 0) {
        return -EINVAL;
    }

    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        return -EBADF;
    }

    if (!(file->flags & VFS_O_WRONLY)) {
        return -EACCES;
    }

    /* 追加模式处理 */
    if (file->flags & VFS_O_APPEND) {
        /* 需要先获取文件大小 */
        struct ipc_message info_req   = {0};
        struct ipc_message info_reply = {0};
        struct vfs_info    info;

        info_req.regs.data[0]  = UDM_VFS_FINFO;
        info_req.regs.data[1]  = file->fs_handle;
        info_reply.buffer.data = &info;
        info_reply.buffer.size = sizeof(info);

        int ret = vfs_ipc_call(file->fs_ep, &info_req, &info_reply);
        if (ret == 0) {
            file->offset = (uint32_t)info.size;
        }
    }

    /* 准备 IPC 消息 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_WRITE;
    req.regs.data[1] = file->fs_handle;
    req.regs.data[2] = file->offset;
    req.regs.data[3] = (uint32_t)size;
    req.buffer.data  = (void *)buf;
    req.buffer.size  = size;

    int ret = vfs_ipc_call(file->fs_ep, &req, &reply);
    if (ret > 0) {
        file->offset += ret;
    }

    return ret;
}

ssize_t vfs_lseek(int fd, ssize_t offset, int whence) {
    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        return -EBADF;
    }

    ssize_t new_offset;

    switch (whence) {
    case VFS_SEEK_SET:
        new_offset = offset;
        break;

    case VFS_SEEK_CUR:
        new_offset = (ssize_t)file->offset + offset;
        break;

    case VFS_SEEK_END: {
        /* 需要获取文件大小 */
        struct ipc_message info_req   = {0};
        struct ipc_message info_reply = {0};
        struct vfs_info    info;

        info_req.regs.data[0]  = UDM_VFS_FINFO;
        info_req.regs.data[1]  = file->fs_handle;
        info_reply.buffer.data = &info;
        info_reply.buffer.size = sizeof(info);

        int ret = vfs_ipc_call(file->fs_ep, &info_req, &info_reply);
        if (ret < 0) {
            return ret;
        }

        new_offset = (ssize_t)info.size + offset;
        break;
    }

    default:
        return -EINVAL;
    }

    if (new_offset < 0) {
        return -EINVAL;
    }

    file->offset = (uint32_t)new_offset;
    return new_offset;
}

int vfs_info(const char *path, struct vfs_info *info) {
    if (!path || path[0] != '/' || !info) {
        return -EINVAL;
    }

    /* 查找挂载点 */
    const char       *rel_path;
    struct vfs_mount *mount = vfs_lookup_mount(path, &rel_path);
    if (!mount) {
        return -ENOENT;
    }

    /* 准备 IPC 消息 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_INFO;
    req.buffer.data  = (void *)rel_path;
    req.buffer.size  = 0;
    while (rel_path[req.buffer.size]) {
        req.buffer.size++;
    }
    req.buffer.size++;

    reply.buffer.data = info;
    reply.buffer.size = sizeof(*info);

    return vfs_ipc_call(mount->fs_ep, &req, &reply);
}

int vfs_finfo(int fd, struct vfs_info *info) {
    if (!info) {
        return -EINVAL;
    }

    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        return -EBADF;
    }

    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0]  = UDM_VFS_FINFO;
    req.regs.data[1]  = file->fs_handle;
    reply.buffer.data = info;
    reply.buffer.size = sizeof(*info);

    return vfs_ipc_call(file->fs_ep, &req, &reply);
}

int vfs_opendir(const char *path) {
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    /* 查找挂载点 */
    const char       *rel_path;
    struct vfs_mount *mount = vfs_lookup_mount(path, &rel_path);
    if (!mount) {
        return -ENOENT;
    }

    /* 分配 fd */
    int fd = fd_alloc(fdt);
    if (fd < 0) {
        return fd;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        fd_free(fdt, fd);
        return -EINVAL;
    }

    /* 准备 IPC 消息 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_OPENDIR;
    req.buffer.data  = (void *)rel_path;
    req.buffer.size  = 0;
    while (rel_path[req.buffer.size]) {
        req.buffer.size++;
    }
    req.buffer.size++;

    int ret = vfs_ipc_call(mount->fs_ep, &req, &reply);
    if (ret < 0) {
        fd_free(fdt, fd);
        return ret;
    }

    /* 填充 vfs_file */
    file->fs_handle = (uint32_t)ret;
    file->fs_ep     = mount->fs_ep;
    file->offset    = 0;
    file->flags     = VFS_O_RDONLY | VFS_O_DIRECTORY;

    return fd;
}

int vfs_readdir(int fd, uint32_t index, struct vfs_dirent *entry) {
    if (!entry) {
        return -EINVAL;
    }

    struct fd_table *fdt = get_current_fd_table();
    if (!fdt) {
        return -ENOENT;
    }

    struct vfs_file *file = fd_get(fdt, fd);
    if (!file) {
        return -EBADF;
    }

    if (!(file->flags & VFS_O_DIRECTORY)) {
        return -ENOTDIR;
    }

    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0]  = UDM_VFS_READDIR;
    req.regs.data[1]  = file->fs_handle;
    req.regs.data[2]  = index;
    reply.buffer.data = entry;
    reply.buffer.size = sizeof(*entry);

    return vfs_ipc_call(file->fs_ep, &req, &reply);
}

int vfs_mkdir(const char *path) {
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    /* 查找挂载点 */
    const char       *rel_path;
    struct vfs_mount *mount = vfs_lookup_mount(path, &rel_path);
    if (!mount) {
        return -ENOENT;
    }

    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_MKDIR;
    req.buffer.data  = (void *)rel_path;
    req.buffer.size  = 0;
    while (rel_path[req.buffer.size]) {
        req.buffer.size++;
    }
    req.buffer.size++;

    return vfs_ipc_call(mount->fs_ep, &req, &reply);
}

int vfs_del(const char *path) {
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    /* 查找挂载点 */
    const char       *rel_path;
    struct vfs_mount *mount = vfs_lookup_mount(path, &rel_path);
    if (!mount) {
        return -ENOENT;
    }

    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_DEL;
    req.buffer.data  = (void *)rel_path;
    req.buffer.size  = 0;
    while (rel_path[req.buffer.size]) {
        req.buffer.size++;
    }
    req.buffer.size++;

    return vfs_ipc_call(mount->fs_ep, &req, &reply);
}
