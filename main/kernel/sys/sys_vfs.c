/**
 * @file kernel/sys/sys_vfs.c
 * @brief VFS 系统调用实现
 */

#include <kernel/sys/syscall.h>
#include <kernel/vfs/vfs.h>
#include <xnix/errno.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

/**
 * 从用户空间复制字符串到内核缓冲区
 */
static int copy_string_from_user(char *dst, const char *user_src, size_t max_len) {
    if (!dst || !user_src || max_len == 0) {
        return -EINVAL;
    }

    /* 逐字符复制直到 null 或达到最大长度 */
    for (size_t i = 0; i < max_len; i++) {
        char c;
        int  ret = copy_from_user(&c, user_src + i, 1);
        if (ret < 0) {
            return ret;
        }
        dst[i] = c;
        if (c == '\0') {
            return 0;
        }
    }

    /* 字符串太长 */
    dst[max_len - 1] = '\0';
    return -ENAMETOOLONG;
}

/* SYS_OPEN: ebx=path, ecx=flags */
static int32_t sys_open(const uint32_t *args) {
    const char *user_path = (const char *)(uintptr_t)args[0];
    uint32_t    flags     = args[1];

    /* 复制路径到内核 */
    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    return vfs_open(path, flags);
}

/* SYS_CLOSE: ebx=fd */
static int32_t sys_close(const uint32_t *args) {
    int fd = (int)args[0];
    return vfs_close(fd);
}

/* SYS_READ: ebx=fd, ecx=buf, edx=size */
static int32_t sys_read(const uint32_t *args) {
    int      fd       = (int)args[0];
    void    *user_buf = (void *)(uintptr_t)args[1];
    uint32_t size     = args[2];

    if (size == 0) {
        return 0;
    }

    /* 限制单次读取大小 */
    if (size > 4096) {
        size = 4096;
    }

    /* 分配内核缓冲区 */
    char    kbuf[4096];
    ssize_t ret = vfs_read(fd, kbuf, size);
    if (ret > 0) {
        int err = copy_to_user(user_buf, kbuf, ret);
        if (err < 0) {
            return err;
        }
    }

    return (int32_t)ret;
}

/* SYS_WRITE2: ebx=fd, ecx=buf, edx=size */
static int32_t sys_write2(const uint32_t *args) {
    int         fd       = (int)args[0];
    const void *user_buf = (const void *)(uintptr_t)args[1];
    uint32_t    size     = args[2];

    if (size == 0) {
        return 0;
    }

    /* 限制单次写入大小 */
    if (size > 4096) {
        size = 4096;
    }

    /* 复制数据到内核 */
    char kbuf[4096];
    int  err = copy_from_user(kbuf, user_buf, size);
    if (err < 0) {
        return err;
    }

    return (int32_t)vfs_write(fd, kbuf, size);
}

/* SYS_LSEEK: ebx=fd, ecx=offset, edx=whence */
static int32_t sys_lseek(const uint32_t *args) {
    int     fd     = (int)args[0];
    ssize_t offset = (ssize_t)(int32_t)args[1];
    int     whence = (int)args[2];

    return (int32_t)vfs_lseek(fd, offset, whence);
}

/* SYS_INFO: ebx=path, ecx=info */
static int32_t sys_info(const uint32_t *args) {
    const char      *user_path = (const char *)(uintptr_t)args[0];
    struct vfs_info *user_info = (struct vfs_info *)(uintptr_t)args[1];

    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    struct vfs_info info;
    ret = vfs_info(path, &info);
    if (ret < 0) {
        return ret;
    }

    ret = copy_to_user(user_info, &info, sizeof(info));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/* SYS_FINFO: ebx=fd, ecx=info */
static int32_t sys_finfo(const uint32_t *args) {
    int              fd        = (int)args[0];
    struct vfs_info *user_info = (struct vfs_info *)(uintptr_t)args[1];

    struct vfs_info info;
    int             ret = vfs_finfo(fd, &info);
    if (ret < 0) {
        return ret;
    }

    ret = copy_to_user(user_info, &info, sizeof(info));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/* SYS_OPENDIR: ebx=path */
static int32_t sys_opendir(const uint32_t *args) {
    const char *user_path = (const char *)(uintptr_t)args[0];

    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    return vfs_opendir(path);
}

/* SYS_READDIR: ebx=fd, ecx=index, edx=entry */
static int32_t sys_readdir(const uint32_t *args) {
    int                fd         = (int)args[0];
    uint32_t           index      = args[1];
    struct vfs_dirent *user_entry = (struct vfs_dirent *)(uintptr_t)args[2];

    struct vfs_dirent entry;
    int               ret = vfs_readdir(fd, index, &entry);
    if (ret < 0) {
        return ret;
    }

    ret = copy_to_user(user_entry, &entry, sizeof(entry));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/* SYS_MKDIR: ebx=path */
static int32_t sys_mkdir(const uint32_t *args) {
    const char *user_path = (const char *)(uintptr_t)args[0];

    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    return vfs_mkdir(path);
}

/* SYS_DEL: ebx=path */
static int32_t sys_del(const uint32_t *args) {
    const char *user_path = (const char *)(uintptr_t)args[0];

    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    return vfs_del(path);
}

/* SYS_MOUNT: ebx=path, ecx=fs_ep */
static int32_t sys_mount(const uint32_t *args) {
    const char  *user_path = (const char *)(uintptr_t)args[0];
    cap_handle_t fs_ep     = (cap_handle_t)args[1];

    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    return vfs_mount(path, fs_ep);
}

/* SYS_UMOUNT: ebx=path */
static int32_t sys_umount(const uint32_t *args) {
    const char *user_path = (const char *)(uintptr_t)args[0];

    char path[VFS_PATH_MAX];
    int  ret = copy_string_from_user(path, user_path, VFS_PATH_MAX);
    if (ret < 0) {
        return ret;
    }

    return vfs_umount(path);
}

/**
 * 注册 VFS 系统调用
 */
void sys_vfs_init(void) {
    syscall_register(SYS_OPEN, sys_open, 2, "open");
    syscall_register(SYS_CLOSE, sys_close, 1, "close");
    syscall_register(SYS_READ, sys_read, 3, "read");
    syscall_register(SYS_WRITE2, sys_write2, 3, "write2");
    syscall_register(SYS_LSEEK, sys_lseek, 3, "lseek");
    syscall_register(SYS_INFO, sys_info, 2, "info");
    syscall_register(SYS_FINFO, sys_finfo, 2, "finfo");
    syscall_register(SYS_OPENDIR, sys_opendir, 1, "opendir");
    syscall_register(SYS_READDIR, sys_readdir, 3, "readdir");
    syscall_register(SYS_MKDIR, sys_mkdir, 1, "mkdir");
    syscall_register(SYS_DEL, sys_del, 1, "del");
    syscall_register(SYS_MOUNT, sys_mount, 2, "mount");
    syscall_register(SYS_UMOUNT, sys_umount, 1, "umount");
}
