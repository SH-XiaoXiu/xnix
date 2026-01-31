/**
 * @file fatfs.c
 * @brief FatFs VFS 接口实现
 *
 * 将 VFS 操作映射到 FatFs API
 */

#include "fatfs_vfs.h"

#include <stdio.h>
#include <string.h>
#include <xnix/errno.h>

/* FatFs 错误码转换为 errno */
static int fresult_to_errno(FRESULT res) {
    switch (res) {
    case FR_OK:
        return 0;
    case FR_DISK_ERR:
    case FR_INT_ERR:
        return -EIO;
    case FR_NOT_READY:
        return -ENODEV;
    case FR_NO_FILE:
    case FR_NO_PATH:
        return -ENOENT;
    case FR_INVALID_NAME:
        return -EINVAL;
    case FR_DENIED:
        return -EACCES;
    case FR_EXIST:
        return -EEXIST;
    case FR_INVALID_OBJECT:
        return -EBADF;
    case FR_WRITE_PROTECTED:
        return -EROFS;
    case FR_INVALID_DRIVE:
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM:
        return -ENODEV;
    case FR_NOT_ENOUGH_CORE:
        return -ENOMEM;
    case FR_TOO_MANY_OPEN_FILES:
        return -EMFILE;
    case FR_INVALID_PARAMETER:
        return -EINVAL;
    default:
        return -EIO;
    }
}

/* 分配句柄 */
static int alloc_handle(struct fatfs_ctx *ctx) {
    for (int i = 0; i < FATFS_MAX_HANDLES; i++) {
        if (!ctx->handles[i].in_use) {
            ctx->handles[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/* 释放句柄 */
static void free_handle(struct fatfs_ctx *ctx, int h) {
    if (h >= 0 && h < FATFS_MAX_HANDLES) {
        ctx->handles[h].in_use = 0;
    }
}

/* 获取句柄 */
static struct fatfs_handle *get_handle(struct fatfs_ctx *ctx, uint32_t h) {
    if (h >= FATFS_MAX_HANDLES || !ctx->handles[h].in_use) {
        return NULL;
    }
    return &ctx->handles[h];
}

/* VFS 标志转换为 FatFs 标志 */
static BYTE vfs_flags_to_fatfs(uint32_t vfs_flags) {
    BYTE mode = 0;

    if (vfs_flags & VFS_O_RDONLY) {
        mode |= FA_READ;
    }
    if (vfs_flags & VFS_O_WRONLY) {
        mode |= FA_WRITE;
    }

    if (vfs_flags & VFS_O_CREAT) {
        if (vfs_flags & VFS_O_EXCL) {
            mode |= FA_CREATE_NEW;
        } else if (vfs_flags & VFS_O_TRUNC) {
            mode |= FA_CREATE_ALWAYS;
        } else {
            mode |= FA_OPEN_ALWAYS;
        }
    } else if (vfs_flags & VFS_O_TRUNC) {
        mode |= FA_CREATE_ALWAYS;
    } else if (vfs_flags & VFS_O_APPEND) {
        mode |= FA_OPEN_APPEND;
    }

    return mode;
}

/* 打开文件 */
static int fatfs_open(void *ctx, const char *path, uint32_t flags) {
    struct fatfs_ctx *fctx = (struct fatfs_ctx *)ctx;

    int h = alloc_handle(fctx);
    if (h < 0) {
        return -EMFILE;
    }

    struct fatfs_handle *handle = &fctx->handles[h];
    BYTE                 mode   = vfs_flags_to_fatfs(flags);

    FRESULT res = f_open(&handle->obj.file, path, mode);
    if (res != FR_OK) {
        free_handle(fctx, h);
        return fresult_to_errno(res);
    }

    handle->type  = 0; /* file */
    handle->flags = flags;
    return h;
}

/* 关闭文件 */
static int fatfs_close(void *ctx, uint32_t h) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle) {
        return -EBADF;
    }

    FRESULT res;
    if (handle->type == 0) {
        res = f_close(&handle->obj.file);
    } else {
        res = f_closedir(&handle->obj.dir);
    }

    free_handle(fctx, h);
    return fresult_to_errno(res);
}

/* 读取文件 */
static int fatfs_read(void *ctx, uint32_t h, void *buf, uint32_t offset, uint32_t size) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle || handle->type != 0) {
        return -EBADF;
    }

    /* 移动到指定偏移 */
    FRESULT res = f_lseek(&handle->obj.file, offset);
    if (res != FR_OK) {
        return fresult_to_errno(res);
    }

    UINT br;
    res = f_read(&handle->obj.file, buf, size, &br);
    if (res != FR_OK) {
        return fresult_to_errno(res);
    }

    return (int)br;
}

/* 写入文件 */
static int fatfs_write(void *ctx, uint32_t h, const void *buf, uint32_t offset, uint32_t size) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle || handle->type != 0) {
        return -EBADF;
    }

    /* 追加模式:移动到末尾 */
    if (handle->flags & VFS_O_APPEND) {
        offset = f_size(&handle->obj.file);
    }

    /* 移动到指定偏移 */
    FRESULT res = f_lseek(&handle->obj.file, offset);
    if (res != FR_OK) {
        return fresult_to_errno(res);
    }

    UINT bw;
    res = f_write(&handle->obj.file, buf, size, &bw);
    if (res != FR_OK) {
        return fresult_to_errno(res);
    }

    return (int)bw;
}

/* 获取文件信息(通过路径) */
static int fatfs_info(void *ctx, const char *path, struct vfs_info *info) {
    (void)ctx;

    FILINFO fno;
    FRESULT res = f_stat(path, &fno);
    if (res != FR_OK) {
        return fresult_to_errno(res);
    }

    info->type  = (fno.fattrib & AM_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    info->mode  = 0;
    info->size  = fno.fsize;
    info->ctime = 0;
    info->mtime = 0;
    info->atime = 0;

    return 0;
}

/* 获取文件信息(通过句柄) */
static int fatfs_finfo(void *ctx, uint32_t h, struct vfs_info *info) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle) {
        return -EBADF;
    }

    if (handle->type == 0) {
        info->type = VFS_TYPE_FILE;
        info->size = f_size(&handle->obj.file);
    } else {
        info->type = VFS_TYPE_DIR;
        info->size = 0;
    }

    info->mode  = 0;
    info->ctime = 0;
    info->mtime = 0;
    info->atime = 0;

    return 0;
}

/* 打开目录 */
static int fatfs_opendir(void *ctx, const char *path) {
    struct fatfs_ctx *fctx = (struct fatfs_ctx *)ctx;

    int h = alloc_handle(fctx);
    if (h < 0) {
        return -EMFILE;
    }

    struct fatfs_handle *handle = &fctx->handles[h];

    FRESULT res = f_opendir(&handle->obj.dir, path);
    if (res != FR_OK) {
        free_handle(fctx, h);
        return fresult_to_errno(res);
    }

    handle->type  = 1; /* dir */
    handle->flags = 0;
    return h;
}

/* 读取目录项 */
static int fatfs_readdir(void *ctx, uint32_t h, uint32_t index, struct vfs_dirent *entry) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle || handle->type != 1) {
        return -EBADF;
    }

    /* 重置目录读取位置 */
    f_rewinddir(&handle->obj.dir);

    /* 跳过前 index 项 */
    FILINFO fno;
    for (uint32_t i = 0; i <= index; i++) {
        FRESULT res = f_readdir(&handle->obj.dir, &fno);
        if (res != FR_OK) {
            return fresult_to_errno(res);
        }
        if (fno.fname[0] == '\0') {
            return -ENOENT; /* 没有更多项 */
        }
    }

    entry->type     = (fno.fattrib & AM_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    entry->name_len = strlen(fno.fname);
    strncpy(entry->name, fno.fname, VFS_NAME_MAX);
    entry->name[VFS_NAME_MAX] = '\0';

    return 0;
}

/* 创建目录 */
static int fatfs_mkdir(void *ctx, const char *path) {
    (void)ctx;

    FRESULT res = f_mkdir(path);
    return fresult_to_errno(res);
}

/* 删除文件或目录 */
static int fatfs_del(void *ctx, const char *path) {
    (void)ctx;

    FRESULT res = f_unlink(path);
    return fresult_to_errno(res);
}

/* 截断文件 */
static int fatfs_truncate(void *ctx, uint32_t h, uint64_t new_size) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle || handle->type != 0) {
        return -EBADF;
    }

    /* 移动到新大小位置 */
    FRESULT res = f_lseek(&handle->obj.file, (FSIZE_t)new_size);
    if (res != FR_OK) {
        return fresult_to_errno(res);
    }

    /* 截断 */
    res = f_truncate(&handle->obj.file);
    return fresult_to_errno(res);
}

/* 同步文件 */
static int fatfs_sync(void *ctx, uint32_t h) {
    struct fatfs_ctx    *fctx   = (struct fatfs_ctx *)ctx;
    struct fatfs_handle *handle = get_handle(fctx, h);

    if (!handle || handle->type != 0) {
        return -EBADF;
    }

    FRESULT res = f_sync(&handle->obj.file);
    return fresult_to_errno(res);
}

/* 重命名 */
static int fatfs_rename(void *ctx, const char *old_path, const char *new_path) {
    (void)ctx;

    FRESULT res = f_rename(old_path, new_path);
    return fresult_to_errno(res);
}

/* VFS 操作表 */
static struct vfs_operations g_fatfs_ops = {
    .open     = fatfs_open,
    .close    = fatfs_close,
    .read     = fatfs_read,
    .write    = fatfs_write,
    .info     = fatfs_info,
    .finfo    = fatfs_finfo,
    .opendir  = fatfs_opendir,
    .readdir  = fatfs_readdir,
    .mkdir    = fatfs_mkdir,
    .del      = fatfs_del,
    .truncate = fatfs_truncate,
    .sync     = fatfs_sync,
    .rename   = fatfs_rename,
};

int fatfs_init(struct fatfs_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    /* 挂载文件系统 */
    FRESULT res = f_mount(&ctx->fs, "", 1);
    if (res != FR_OK) {
        printf("[fatfsd] Failed to mount FAT filesystem: %d\n", res);
        return fresult_to_errno(res);
    }

    ctx->mounted = 1;
    printf("[fatfsd] FAT filesystem mounted\n");
    return 0;
}

struct vfs_operations *fatfs_get_ops(void) {
    return &g_fatfs_ops;
}
