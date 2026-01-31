/**
 * @file fd.c
 * @brief 文件描述符表管理
 */

#include <kernel/vfs/vfs.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/sync.h>

struct fd_table *fd_table_create(void) {
    struct fd_table *fdt = kzalloc(sizeof(struct fd_table));
    if (!fdt) {
        return NULL;
    }

    spin_init(&fdt->lock);
    return fdt;
}

void fd_table_destroy(struct fd_table *fdt) {
    if (!fdt) {
        return;
    }

    spin_lock(&fdt->lock);

    for (int i = 0; i < VFS_MAX_FD; i++) {
        struct vfs_file *file = fdt->files[i];
        if (file) {
            file->refcount--;
            if (file->refcount == 0) {
                kfree(file);
            }
            fdt->files[i] = NULL;
        }
    }

    spin_unlock(&fdt->lock);
    kfree(fdt);
}

int fd_alloc(struct fd_table *fdt) {
    if (!fdt) {
        return -EINVAL;
    }

    spin_lock(&fdt->lock);

    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (!fdt->files[i]) {
            /* 分配 vfs_file 结构 */
            struct vfs_file *file = kzalloc(sizeof(struct vfs_file));
            if (!file) {
                spin_unlock(&fdt->lock);
                return -ENOMEM;
            }
            file->refcount = 1;
            fdt->files[i]  = file;
            spin_unlock(&fdt->lock);
            return i;
        }
    }

    spin_unlock(&fdt->lock);
    return -EMFILE; /* Too many open files */
}

struct vfs_file *fd_get(struct fd_table *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FD) {
        return NULL;
    }

    spin_lock(&fdt->lock);
    struct vfs_file *file = fdt->files[fd];
    spin_unlock(&fdt->lock);

    return file;
}

void fd_free(struct fd_table *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= VFS_MAX_FD) {
        return;
    }

    spin_lock(&fdt->lock);

    struct vfs_file *file = fdt->files[fd];
    if (file) {
        file->refcount--;
        if (file->refcount == 0) {
            kfree(file);
        }
        fdt->files[fd] = NULL;
    }

    spin_unlock(&fdt->lock);
}
