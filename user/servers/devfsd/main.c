/**
 * @file main.c
 * @brief devfsd: 设备文件系统服务
 *
 * 提供设备文件系统，挂载到 /dev。
 *
 * 功能：
 *   - 列出已注册的块设备 (sda, sdb, etc.)
 *   - 提供基础设备文件 (null, zero)
 *   - 代理块设备读写操作
 *
 * 使用：
 *   mount -t devfs /dev
 */

#include <block.h>
#include <d/server.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

#define DEVFS_MAX_FILES 32
#define DEVFS_NAME_MAX  16

/* 设备文件类型 */
typedef enum {
    DEV_TYPE_BLOCK,     /* 块设备 */
    DEV_TYPE_NULL,      /* /dev/null */
    DEV_TYPE_ZERO,      /* /dev/zero */
} dev_type_t;

/* 设备文件条目 */
struct dev_entry {
    char name[DEVFS_NAME_MAX];
    dev_type_t type;
    struct block_device *bdev;  /* 仅块设备使用 */
    bool valid;
};

/* devfs 上下文 */
struct devfs_ctx {
    struct dev_entry entries[DEVFS_MAX_FILES];
    int count;
    handle_t endpoint;
};

static struct devfs_ctx g_devfs;

/* 查找设备文件 */
static struct dev_entry *devfs_find(const char *name) {
    for (int i = 0; i < DEVFS_MAX_FILES; i++) {
        if (g_devfs.entries[i].valid &&
            strcmp(g_devfs.entries[i].name, name) == 0) {
            return &g_devfs.entries[i];
        }
    }
    return NULL;
}

/* 扫描并注册块设备 */
static void devfs_scan_block_devices(void) {
    struct block_device *dev = block_first();
    while (dev) {
        /* 查找空闲槽位 */
        for (int i = 0; i < DEVFS_MAX_FILES; i++) {
            if (!g_devfs.entries[i].valid) {
                strncpy(g_devfs.entries[i].name, dev->name, DEVFS_NAME_MAX - 1);
                g_devfs.entries[i].name[DEVFS_NAME_MAX - 1] = '\0';
                g_devfs.entries[i].type = DEV_TYPE_BLOCK;
                g_devfs.entries[i].bdev = dev;
                g_devfs.entries[i].valid = true;
                g_devfs.count++;
                printf("[devfsd] registered: /dev/%s (%s, %llu sectors)\n",
                       dev->name,
                       block_type_name(dev->type),
                       (unsigned long long)dev->info.sector_count);
                break;
            }
        }
        dev = block_next(dev);
    }
}

/* 注册特殊设备 */
static void devfs_register_special(const char *name, dev_type_t type) {
    for (int i = 0; i < DEVFS_MAX_FILES; i++) {
        if (!g_devfs.entries[i].valid) {
            strncpy(g_devfs.entries[i].name, name, DEVFS_NAME_MAX - 1);
            g_devfs.entries[i].name[DEVFS_NAME_MAX - 1] = '\0';
            g_devfs.entries[i].type = type;
            g_devfs.entries[i].bdev = NULL;
            g_devfs.entries[i].valid = true;
            g_devfs.count++;
            printf("[devfsd] registered: /dev/%s\n", name);
            return;
        }
    }
}

/* ============== VFS 操作实现 ============== */

static int devfs_open(void *ctx, const char *path, uint32_t flags) {
    (void)ctx;
    (void)flags;

    /* 跳过前导 '/' */
    if (path[0] == '/') {
        path++;
    }

    /* 根目录 */
    if (path[0] == '\0') {
        return 0;  /* 根目录句柄 */
    }

    /* 查找设备 */
    struct dev_entry *ent = devfs_find(path);
    if (!ent) {
        return -1;  /* 文件不存在 */
    }

    /* 返回条目索引作为句柄 */
    return (int)(ent - g_devfs.entries);
}

static int devfs_close(void *ctx, uint32_t handle) {
    (void)ctx;
    (void)handle;
    return 0;
}

static int devfs_read(void *ctx, uint32_t handle, void *buf, uint32_t offset, uint32_t size) {
    (void)ctx;

    if (handle >= DEVFS_MAX_FILES || !g_devfs.entries[handle].valid) {
        return -1;
    }

    struct dev_entry *ent = &g_devfs.entries[handle];

    switch (ent->type) {
    case DEV_TYPE_NULL:
        return 0;  /* /dev/null 返回 EOF */

    case DEV_TYPE_ZERO:
        memset(buf, 0, size);
        return (int)size;  /* /dev/zero 返回零 */

    case DEV_TYPE_BLOCK:
        if (ent->bdev && ent->bdev->ops && ent->bdev->ops->read) {
            /* 计算扇区号和偏移 */
            uint32_t sector_size = ent->bdev->info.sector_size;
            if (sector_size == 0) sector_size = 512;

            uint64_t lba = offset / sector_size;
            uint32_t sector_offset = offset % sector_size;
            uint32_t sectors = (size + sector_size - 1) / sector_size + 1;

            /* 读取扇区 */
            char *temp = malloc(sectors * sector_size);
            if (!temp) return -1;

            int ret = ent->bdev->ops->read(ent->bdev->driver_ctx, lba, sectors, temp);
            if (ret < 0) {
                free(temp);
                return ret;
            }

            /* 复制请求的数据 */
            uint32_t copy_size = size;
            if (sector_offset + size > sectors * sector_size) {
                copy_size = sectors * sector_size - sector_offset;
            }
            memcpy(buf, temp + sector_offset, copy_size);
            free(temp);
            return (int)copy_size;
        }
        return -1;

    default:
        return -1;
    }
}

static int devfs_write(void *ctx, uint32_t handle, const void *buf, uint32_t offset, uint32_t size) {
    (void)ctx;

    if (handle >= DEVFS_MAX_FILES || !g_devfs.entries[handle].valid) {
        return -1;
    }

    struct dev_entry *ent = &g_devfs.entries[handle];

    switch (ent->type) {
    case DEV_TYPE_NULL:
    case DEV_TYPE_ZERO:
        return (int)size;  /* 吞掉数据 */

    case DEV_TYPE_BLOCK:
        if (ent->bdev && ent->bdev->ops && ent->bdev->ops->write) {
            uint32_t sector_size = ent->bdev->info.sector_size;
            if (sector_size == 0) sector_size = 512;

            uint64_t lba = offset / sector_size;
            uint32_t sector_offset = offset % sector_size;
            uint32_t sectors = (sector_offset + size + sector_size - 1) / sector_size;

            char *temp = malloc(sectors * sector_size);
            if (!temp) return -1;

            /* 非对齐写入需先读出原始扇区，保留非写入区域数据 */
            if ((sector_offset != 0 || size % sector_size != 0) && ent->bdev->ops->read) {
                int ret = ent->bdev->ops->read(ent->bdev->driver_ctx, lba, sectors, temp);
                if (ret < 0) {
                    free(temp);
                    return ret;
                }
            }

            memcpy(temp + sector_offset, buf, size);

            int ret = ent->bdev->ops->write(ent->bdev->driver_ctx, lba, sectors, temp);
            free(temp);
            if (ret < 0) return ret;
            return (int)size;
        }
        return -1;

    default:
        return -1;
    }
}

static int devfs_info(void *ctx, const char *path, struct vfs_info *info) {
    (void)ctx;

    /* 跳过前导 '/' */
    if (path[0] == '/') {
        path++;
    }

    /* 根目录 */
    if (path[0] == '\0') {
        info->size = 0;
        info->type = VFS_TYPE_DIR;
        return 0;
    }

    struct dev_entry *ent = devfs_find(path);
    if (!ent) {
        return -1;
    }

    info->type = VFS_TYPE_FILE;
    if (ent->type == DEV_TYPE_BLOCK && ent->bdev) {
        info->size = ent->bdev->info.sector_count * ent->bdev->info.sector_size;
    } else {
        info->size = 0;
    }

    return 0;
}

static int devfs_finfo(void *ctx, uint32_t handle, struct vfs_info *info) {
    (void)ctx;

    if (handle >= DEVFS_MAX_FILES || !g_devfs.entries[handle].valid) {
        return -1;
    }

    struct dev_entry *ent = &g_devfs.entries[handle];
    info->type = VFS_TYPE_FILE;

    if (ent->type == DEV_TYPE_BLOCK && ent->bdev) {
        info->size = ent->bdev->info.sector_count * ent->bdev->info.sector_size;
    } else {
        info->size = 0;
    }

    return 0;
}

static int devfs_opendir(void *ctx, const char *path) {
    (void)ctx;

    /* 只支持根目录 */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        return 0;
    }

    return -1;
}

static int devfs_readdir(void *ctx, uint32_t handle, uint32_t index, struct vfs_dirent *entry) {
    (void)ctx;
    (void)handle;

    int count = 0;
    for (int i = 0; i < DEVFS_MAX_FILES; i++) {
        if (g_devfs.entries[i].valid) {
            if (count == (int)index) {
                strncpy(entry->name, g_devfs.entries[i].name, sizeof(entry->name) - 1);
                entry->name[sizeof(entry->name) - 1] = '\0';
                entry->type = VFS_TYPE_FILE;
                return 0;
            }
            count++;
        }
    }

    return -1;  /* 没有更多条目 */
}

static int devfs_mkdir(void *ctx, const char *path) {
    (void)ctx;
    (void)path;
    return -1;  /* 不支持 */
}

static int devfs_del(void *ctx, const char *path) {
    (void)ctx;
    (void)path;
    return -1;  /* 不支持 */
}

static int devfs_truncate(void *ctx, uint32_t handle, uint64_t new_size) {
    (void)ctx;
    (void)handle;
    (void)new_size;
    return -1;  /* 不支持 */
}

static int devfs_sync(void *ctx, uint32_t handle) {
    (void)ctx;
    (void)handle;
    return 0;
}

static int devfs_rename(void *ctx, const char *old_path, const char *new_path) {
    (void)ctx;
    (void)old_path;
    (void)new_path;
    return -1;  /* 不支持 */
}

/* VFS 操作表 */
static struct vfs_operations devfs_ops = {
    .open     = devfs_open,
    .close    = devfs_close,
    .read     = devfs_read,
    .write    = devfs_write,
    .info     = devfs_info,
    .finfo    = devfs_finfo,
    .opendir  = devfs_opendir,
    .readdir  = devfs_readdir,
    .mkdir    = devfs_mkdir,
    .del      = devfs_del,
    .truncate = devfs_truncate,
    .sync     = devfs_sync,
    .rename   = devfs_rename,
};

/* VFS 请求处理回调 */
static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(&devfs_ops, &g_devfs, msg);
}

/* devfsd 服务线程入口 */
static void *devfsd_thread(void *arg) {
    (void)arg;

    printf("[devfsd] service thread started\n");

    /* 使用 udm_server 框架处理请求 */
    struct udm_server srv = {
        .endpoint = g_devfs.endpoint,
        .handler  = vfs_handler,
        .name     = "devfsd",
    };

    udm_server_init(&srv);
    udm_server_run(&srv);

    printf("[devfsd] service thread exiting\n");
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    env_set_name("devfsd");

    /* 初始化 devfs */
    memset(&g_devfs, 0, sizeof(g_devfs));

    /* 注册特殊设备 */
    devfs_register_special("null", DEV_TYPE_NULL);
    devfs_register_special("zero", DEV_TYPE_ZERO);

    /* 扫描块设备 */
    devfs_scan_block_devices();

    /* 创建 endpoint */
    g_devfs.endpoint = sys_endpoint_create("devfs_ep");
    if (g_devfs.endpoint == HANDLE_INVALID) {
        printf("[devfsd] FATAL: failed to create endpoint\n");
        return 1;
    }

    printf("[devfsd] created endpoint: %u\n", g_devfs.endpoint);

    /* 通知就绪 */
    svc_notify_ready("devfsd");

    /* 运行服务线程 */
    pthread_t thread;
    if (pthread_create(&thread, NULL, devfsd_thread, NULL) != 0) {
        printf("[devfsd] FATAL: failed to create thread\n");
        return 1;
    }

    /* 等待线程结束 */
    pthread_join(thread, NULL);

    return 0;
}
