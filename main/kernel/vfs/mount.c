/**
 * @file mount.c
 * @brief 挂载点管理
 */

#include <kernel/capability/capability.h>
#include <kernel/ipc/endpoint.h>
#include <kernel/vfs/vfs.h>
#include <xnix/errno.h>
#include <xnix/process.h>
#include <xnix/sync.h>

/* 全局挂载表 */
static struct vfs_mount mounts[VFS_MAX_MOUNTS];
static spinlock_t       mounts_lock;

void vfs_mount_init(void) {
    spin_init(&mounts_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = false;
    }
}

static size_t kstrlen(const char *s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

static void kstrcpy(char *dst, const char *src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

int vfs_mount(const char *path, cap_handle_t fs_ep_handle) {
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    /* 从调用者的 cap 表中查找 endpoint */
    struct process      *proc = (struct process *)process_current();
    struct ipc_endpoint *ep   = cap_lookup(proc, fs_ep_handle, CAP_TYPE_ENDPOINT, CAP_WRITE);
    if (!ep) {
        return -EINVAL;
    }

    size_t path_len = kstrlen(path);
    if (path_len >= VFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    /* 去掉尾部斜杠(除了根目录) */
    while (path_len > 1 && path[path_len - 1] == '/') {
        path_len--;
    }

    spin_lock(&mounts_lock);

    /* 检查是否已挂载 */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && mounts[i].path_len == path_len) {
            bool match = true;
            for (size_t j = 0; j < path_len; j++) {
                if (mounts[i].path[j] != path[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                spin_unlock(&mounts_lock);
                return -EBUSY;
            }
        }
    }

    /* 找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        spin_unlock(&mounts_lock);
        return -ENOMEM;
    }

    /* 增加 endpoint 引用计数 */
    endpoint_ref(ep);

    /* 注册挂载点 */
    kstrcpy(mounts[slot].path, path, path_len + 1);
    mounts[slot].path_len = path_len;
    mounts[slot].fs_ep    = ep;
    mounts[slot].active   = true;

    spin_unlock(&mounts_lock);
    return 0;
}

int vfs_umount(const char *path) {
    if (!path || path[0] != '/') {
        return -EINVAL;
    }

    size_t path_len = kstrlen(path);

    /* 去掉尾部斜杠(除了根目录) */
    while (path_len > 1 && path[path_len - 1] == '/') {
        path_len--;
    }

    spin_lock(&mounts_lock);

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && mounts[i].path_len == path_len) {
            bool match = true;
            for (size_t j = 0; j < path_len; j++) {
                if (mounts[i].path[j] != path[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                struct ipc_endpoint *ep = mounts[i].fs_ep;
                mounts[i].active        = false;
                mounts[i].fs_ep         = NULL;
                spin_unlock(&mounts_lock);
                /* 释放 endpoint 引用 */
                endpoint_unref(ep);
                return 0;
            }
        }
    }

    spin_unlock(&mounts_lock);
    return -EINVAL;
}

struct vfs_mount *vfs_lookup_mount(const char *path, const char **rel_path) {
    if (!path || path[0] != '/') {
        return NULL;
    }

    size_t            path_len = kstrlen(path);
    struct vfs_mount *best     = NULL;
    size_t            best_len = 0;

    spin_lock(&mounts_lock);

    /* 找最长匹配的挂载点 */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            continue;
        }

        size_t mount_len = mounts[i].path_len;

        /* 检查路径是否以挂载点开头 */
        if (mount_len > path_len) {
            continue;
        }

        bool match = true;
        for (size_t j = 0; j < mount_len; j++) {
            if (mounts[i].path[j] != path[j]) {
                match = false;
                break;
            }
        }

        if (!match) {
            continue;
        }

        /* 根目录特殊处理:匹配所有以 / 开头的路径 */
        if (mount_len == 1 && mounts[i].path[0] == '/') {
            if (best_len == 0) {
                best     = &mounts[i];
                best_len = 1;
            }
            continue;
        }

        /* 确保是完整路径匹配(非前缀子串) */
        if (mount_len < path_len && path[mount_len] != '/') {
            continue;
        }

        if (mount_len > best_len) {
            best     = &mounts[i];
            best_len = mount_len;
        }
    }

    spin_unlock(&mounts_lock);

    if (best && rel_path) {
        /* 计算相对路径 */
        if (best_len == 1 && best->path[0] == '/') {
            *rel_path = path; /* 根目录挂载,相对路径就是完整路径 */
        } else if (best_len == path_len) {
            *rel_path = "/"; /* 刚好是挂载点本身 */
        } else {
            *rel_path = path + best_len; /* 挂载点后的部分 */
        }
    }

    return best;
}
