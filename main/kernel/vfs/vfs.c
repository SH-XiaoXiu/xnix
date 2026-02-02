/**
 * @file vfs.c
 * @brief VFS 核心逻辑
 *
 * 通过 IPC 与用户态 fs 服务通信,实现文件操作
 */

#include <kernel/ipc/endpoint.h>
#include <kernel/process/process.h>
#include <kernel/vfs/vfs.h>
#include <stddef.h>
#include <xnix/errno.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/udm/vfs.h>

/* mount.c 中的初始化函数 */
extern void vfs_mount_init(void);

void vfs_init(void) {
    vfs_mount_init();
    pr_ok("VFS: initialized\n");
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

    /* 保存目录路径(用于列出挂载点) */
    size_t path_len = strlen(path);
    if (path_len >= VFS_PATH_MAX) {
        path_len = VFS_PATH_MAX - 1;
    }
    memcpy(file->dir_path, path, path_len);
    file->dir_path[path_len] = '\0';
    /* 去掉尾部斜杠(除了根目录) */
    while (path_len > 1 && file->dir_path[path_len - 1] == '/') {
        file->dir_path[--path_len] = '\0';
    }

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

    /* 先尝试从底层文件系统获取 */
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0]  = UDM_VFS_READDIR;
    req.regs.data[1]  = file->fs_handle;
    req.regs.data[2]  = index;
    reply.buffer.data = entry;
    reply.buffer.size = sizeof(*entry);

    int ret = vfs_ipc_call(file->fs_ep, &req, &reply);
    if (ret == 0) {
        return 0; /* 底层文件系统返回了有效条目 */
    }

    /* 底层没有更多条目,尝试列出挂载点 */
    if (ret == -ENOENT) {
        /* 计算已经返回了多少个底层条目 */
        /* 通过递增 index 直到底层返回 -ENOENT 来确定 */
        uint32_t fs_count = 0;
        for (uint32_t i = 0; i < index; i++) {
            req.regs.data[2]  = i;
            reply.buffer.data = entry;
            reply.buffer.size = sizeof(*entry);
            if (vfs_ipc_call(file->fs_ep, &req, &reply) != 0) {
                fs_count = i;
                break;
            }
            fs_count = i + 1;
        }

        /* 挂载点索引 = 请求索引 - 文件系统条目数 */
        uint32_t mount_index = index - fs_count;

        char mount_name[VFS_NAME_MAX];
        if (vfs_get_child_mount(file->dir_path, mount_index, mount_name, sizeof(mount_name)) == 0) {
            strncpy(entry->name, mount_name, VFS_NAME_MAX - 1);
            entry->name[VFS_NAME_MAX - 1] = '\0';
            entry->type                   = VFS_TYPE_DIR;
            return 0;
        }
    }

    return ret;
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

int vfs_load_file(const char *path, void **out_data, uint32_t *out_size) {
    if (!path || path[0] != '/' || !out_data || !out_size) {
        return -EINVAL;
    }

    /* 获取文件信息 */
    struct vfs_info info;
    int             ret = vfs_info(path, &info);
    if (ret < 0) {
        return ret;
    }

    /* 检查是否为普通文件 */
    if (info.type != VFS_TYPE_FILE) {
        return -EISDIR;
    }

    /* 文件大小限制(最大 4MB) */
    if (info.size > (uint64_t)(4 * 1024 * 1024)) {
        return -EFBIG;
    }

    uint32_t file_size = (uint32_t)info.size;
    if (file_size == 0) {
        return -EINVAL;
    }

    /* 分配内核内存 */
    void *data = kmalloc(file_size);
    if (!data) {
        return -ENOMEM;
    }

    /* 打开文件 */
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kfree(data);
        return fd;
    }

    /* 读取文件内容 */
    uint32_t offset = 0;
    while (offset < file_size) {
        uint32_t chunk = file_size - offset;
        if (chunk > 4096) {
            chunk = 4096;
        }

        ssize_t n = vfs_read(fd, (uint8_t *)data + offset, chunk);
        if (n <= 0) {
            vfs_close(fd);
            kfree(data);
            return n < 0 ? (int)n : -EIO;
        }

        offset += (uint32_t)n;
    }

    vfs_close(fd);

    *out_data = data;
    *out_size = file_size;
    return 0;
}
