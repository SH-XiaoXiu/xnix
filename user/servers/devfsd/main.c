/**
 * @file main.c
 * @brief devfs: 设备文件系统服务
 *
 * 提供设备文件系统，挂载到 /dev。
 *
 * 功能：
 *   - 接收块设备驱动的注册 (通过 BLK IPC 协议)
 *   - 解析 MBR 分区表, 暴露分区节点
 *   - 提供基础设备文件 (null, zero)
 *   - 代理块设备读写操作 (通过 BLK IPC 转发到驱动)
 *
 * 使用：
 *   mount -t devfs /dev
 */

#include <stdbool.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/protocol/blk.h>
#include <xnix/protocol/devfs.h>
#include <xnix/protocol/vfs.h>
#include <xnix/svc.h>
#include <xnix/sys/server.h>
#include <xnix/syscall.h>

#define DEVFS_MAX_FILES 32
#define DEVFS_NAME_MAX  16

/* MBR 分区表 */
#define MBR_PART_TABLE_OFFSET 446
#define MBR_SIGNATURE         0xAA55
#define MBR_MAX_PARTITIONS    4

struct mbr_partition {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

/* 设备文件类型 */
typedef enum {
    DEV_TYPE_BLOCK,     /* 块设备 */
    DEV_TYPE_NULL,      /* /dev/null */
    DEV_TYPE_ZERO,      /* /dev/zero */
    DEV_TYPE_TTY,       /* TTY endpoint 设备 */
} dev_type_t;

/* 设备文件条目 */
struct dev_entry {
    char name[DEVFS_NAME_MAX];
    dev_type_t type;
    handle_t blk_ep;            /* 块设备 endpoint (用于 BLK IPC) */
    uint32_t sector_size;       /* 扇区大小 (字节) */
    uint64_t base_lba;          /* 分区起始 LBA (整盘=0) */
    uint64_t part_sectors;      /* 分区扇区数 */
    bool valid;
};

/* devfs 上下文 */
struct devfs_ctx {
    struct dev_entry entries[DEVFS_MAX_FILES];
    int count;
    handle_t endpoint;
};

static struct devfs_ctx g_devfs;

/* BLK IPC IO 缓冲区 (单次最大 4096 字节 = 8 扇区) */
static char g_blk_buf[BLK_IO_BUF_SIZE];

/* ============== 辅助函数 ============== */

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

/* 查找空闲槽位 */
static int devfs_alloc_slot(void) {
    for (int i = 0; i < DEVFS_MAX_FILES; i++) {
        if (!g_devfs.entries[i].valid) return i;
    }
    return -1;
}

/* 通过 BLK IPC 读扇区 */
static int blk_ipc_read(handle_t ep, uint64_t lba, uint32_t count, void *buf) {
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_BLK_READ;
    req.regs.data[1] = (uint32_t)lba;
    req.regs.data[2] = (uint32_t)(lba >> 32);
    req.regs.data[3] = count;

    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = count * 512;

    int ret = sys_ipc_call(ep, &req, &reply, 5000);
    if (ret < 0) return ret;

    int32_t result = (int32_t)reply.regs.data[1];
    return result;
}

/* 通过 BLK IPC 写扇区 */
static int blk_ipc_write(handle_t ep, uint64_t lba, uint32_t count,
                         const void *buf) {
    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_BLK_WRITE;
    req.regs.data[1] = (uint32_t)lba;
    req.regs.data[2] = (uint32_t)(lba >> 32);
    req.regs.data[3] = count;
    req.buffer.data = (uint64_t)(uintptr_t)buf;
    req.buffer.size = count * 512;

    int ret = sys_ipc_call(ep, &req, &reply, 5000);
    if (ret < 0) return ret;

    return (int32_t)reply.regs.data[1];
}

/* ============== 块设备注册 ============== */

/* 注册特殊设备 */
static void devfs_register_special(const char *name, dev_type_t type) {
    int slot = devfs_alloc_slot();
    if (slot < 0) return;

    struct dev_entry *ent = &g_devfs.entries[slot];
    strncpy(ent->name, name, DEVFS_NAME_MAX - 1);
    ent->name[DEVFS_NAME_MAX - 1] = '\0';
    ent->type = type;
    ent->blk_ep = HANDLE_INVALID;
    ent->valid = true;
    g_devfs.count++;
    printf("[devfs] registered: /dev/%s\n", name);
}

/**
 * 处理块设备注册消息 (来自 fatfsd 等驱动)
 *
 * 1. 创建整盘 dev_entry
 * 2. 通过 BLK_READ 读 LBA 0, 解析 MBR
 * 3. 为每个有效分区创建 dev_entry
 */
static int devfs_handle_register_block(struct ipc_message *msg) {
    /* 提取注册信息 */
    uint64_t sector_count = (uint64_t)msg->regs.data[2] << 32 | msg->regs.data[1];
    uint32_t sector_size = msg->regs.data[3];
    uint32_t name_len = msg->regs.data[4];
    if (sector_size == 0) sector_size = 512;

    /* 提取 block endpoint handle */
    handle_t blk_ep = HANDLE_INVALID;
    if (msg->handles.count >= 1) {
        blk_ep = msg->handles.handles[0];
    }

    /* 提取 buffer: [name\0][mbr 512B] */
    if (!msg->buffer.data || msg->buffer.size < name_len + 1 + 512) {
        printf("[devfs] register: invalid buffer\n");
        return -22;
    }

    const char *buf = (const char *)(uintptr_t)msg->buffer.data;
    char dev_name[DEVFS_NAME_MAX] = {0};
    if (name_len >= DEVFS_NAME_MAX) name_len = DEVFS_NAME_MAX - 1;
    memcpy(dev_name, buf, name_len);
    dev_name[name_len] = '\0';

    const uint8_t *mbr = (const uint8_t *)(buf + name_len + 1);

    if (devfs_find(dev_name)) return -17;

    /* 创建整盘条目 */
    int slot = devfs_alloc_slot();
    if (slot < 0) return -28;

    struct dev_entry *disk = &g_devfs.entries[slot];
    strncpy(disk->name, dev_name, DEVFS_NAME_MAX - 1);
    disk->type = DEV_TYPE_BLOCK;
    disk->blk_ep = blk_ep;
    disk->sector_size = sector_size;
    disk->base_lba = 0;
    disk->part_sectors = sector_count;
    disk->valid = true;
    g_devfs.count++;

    printf("[devfs] registered: /dev/%s (%llu sectors)\n",
           dev_name, (unsigned long long)sector_count);

    /* 从 buffer 中解析 MBR */
    uint16_t sig = (uint16_t)mbr[510] | ((uint16_t)mbr[511] << 8);
    if (sig != MBR_SIGNATURE) {
        return 0;
    }

    const struct mbr_partition *parts =
        (const struct mbr_partition *)(mbr + MBR_PART_TABLE_OFFSET);

    for (int p = 0; p < MBR_MAX_PARTITIONS; p++) {
        if (parts[p].type == 0 || parts[p].lba_start == 0 ||
            parts[p].sector_count == 0)
            continue;

        int pslot = devfs_alloc_slot();
        if (pslot < 0) break;

        struct dev_entry *ent = &g_devfs.entries[pslot];
        snprintf(ent->name, DEVFS_NAME_MAX, "%s%d", dev_name, p + 1);
        ent->type = DEV_TYPE_BLOCK;
        ent->blk_ep = blk_ep;
        ent->sector_size = sector_size;
        ent->base_lba = parts[p].lba_start;
        ent->part_sectors = parts[p].sector_count;
        ent->valid = true;
        g_devfs.count++;

        printf("[devfs] registered: /dev/%s (partition, start=%u, %u sectors)\n",
               ent->name, parts[p].lba_start, parts[p].sector_count);
    }

    return 0;
}

/* ============== TTY 设备注册 ============== */

static int devfs_handle_register_tty(struct ipc_message *msg) {
    handle_t tty_ep = HANDLE_INVALID;
    if (msg->handles.count >= 1) {
        tty_ep = msg->handles.handles[0];
    }
    if (tty_ep == HANDLE_INVALID) {
        return -22; /* EINVAL */
    }

    /* 从 buffer 提取设备名 */
    uint32_t name_len = msg->buffer.size;
    if (!msg->buffer.data || name_len == 0 || name_len >= DEVFS_NAME_MAX) {
        return -22;
    }

    char dev_name[DEVFS_NAME_MAX] = {0};
    memcpy(dev_name, (const char *)(uintptr_t)msg->buffer.data, name_len);
    dev_name[name_len] = '\0';

    if (devfs_find(dev_name)) {
        return -17; /* EEXIST */
    }

    int slot = devfs_alloc_slot();
    if (slot < 0) {
        return -28; /* ENOSPC */
    }

    struct dev_entry *ent = &g_devfs.entries[slot];
    strncpy(ent->name, dev_name, DEVFS_NAME_MAX - 1);
    ent->name[DEVFS_NAME_MAX - 1] = '\0';
    ent->type = DEV_TYPE_TTY;
    ent->blk_ep = tty_ep; /* 复用: 存储 TTY endpoint */
    ent->valid = true;
    g_devfs.count++;

    printf("[devfs] registered: /dev/%s (tty)\n", dev_name);
    return 0;
}

/* ============== VFS 操作实现 ============== */

static int devfs_open(void *ctx, const char *path, uint32_t flags) {
    (void)ctx;
    (void)flags;

    if (path[0] == '/') path++;
    if (path[0] == '\0') return 0;

    struct dev_entry *ent = devfs_find(path);
    if (!ent) return -1;

    return (int)(ent - g_devfs.entries);
}

static int devfs_close(void *ctx, uint32_t handle) {
    (void)ctx;
    (void)handle;
    return 0;
}

static int devfs_read(void *ctx, uint32_t handle, void *buf,
                      uint32_t offset, uint32_t size) {
    (void)ctx;

    if (handle >= DEVFS_MAX_FILES || !g_devfs.entries[handle].valid)
        return -1;

    struct dev_entry *ent = &g_devfs.entries[handle];

    switch (ent->type) {
    case DEV_TYPE_NULL:
        return 0;

    case DEV_TYPE_ZERO:
        memset(buf, 0, size);
        return (int)size;

    case DEV_TYPE_BLOCK: {
        if (ent->blk_ep == HANDLE_INVALID) return -1;

        uint32_t ss = ent->sector_size;
        if (ss == 0) ss = 512;

        uint64_t lba = ent->base_lba + offset / ss;
        uint32_t sector_offset = offset % ss;
        uint32_t sectors = (sector_offset + size + ss - 1) / ss;

        /* 越界保护 */
        uint64_t part_end = ent->base_lba + ent->part_sectors;
        if (lba >= part_end) return 0;
        if (lba + sectors > part_end)
            sectors = (uint32_t)(part_end - lba);

        /* 分批读取 (每次最多 BLK_IO_MAX_SECTORS 扇区) */
        uint32_t total_copied = 0;
        char *out = (char *)buf;

        while (sectors > 0 && total_copied < size) {
            uint32_t batch = sectors;
            if (batch > BLK_IO_MAX_SECTORS) batch = BLK_IO_MAX_SECTORS;

            int ret = blk_ipc_read(ent->blk_ep, lba, batch, g_blk_buf);
            if (ret < 0) return (total_copied > 0) ? (int)total_copied : ret;

            /* 从扇区数据中提取请求范围 */
            uint32_t avail = batch * ss;
            uint32_t skip = sector_offset;
            uint32_t copy = avail - skip;
            if (copy > size - total_copied) copy = size - total_copied;

            memcpy(out + total_copied, g_blk_buf + skip, copy);
            total_copied += copy;

            lba += batch;
            sectors -= batch;
            sector_offset = 0; /* 后续批次从扇区头开始 */
        }

        return (int)total_copied;
    }

    default:
        return -1;
    }
}

static int devfs_write(void *ctx, uint32_t handle, const void *buf,
                       uint32_t offset, uint32_t size) {
    (void)ctx;

    if (handle >= DEVFS_MAX_FILES || !g_devfs.entries[handle].valid)
        return -1;

    struct dev_entry *ent = &g_devfs.entries[handle];

    switch (ent->type) {
    case DEV_TYPE_NULL:
    case DEV_TYPE_ZERO:
        return (int)size;

    case DEV_TYPE_BLOCK: {
        if (ent->blk_ep == HANDLE_INVALID) return -1;

        uint32_t ss = ent->sector_size;
        if (ss == 0) ss = 512;

        uint64_t lba = ent->base_lba + offset / ss;
        uint32_t sector_offset = offset % ss;
        uint32_t sectors = (sector_offset + size + ss - 1) / ss;

        /* 越界保护 */
        uint64_t part_end = ent->base_lba + ent->part_sectors;
        if (lba >= part_end) return 0;
        if (lba + sectors > part_end) {
            sectors = (uint32_t)(part_end - lba);
            uint32_t max_bytes = sectors * ss - sector_offset;
            if (size > max_bytes) size = max_bytes;
        }

        /* 分批写入 */
        uint32_t total_written = 0;
        const char *in = (const char *)buf;

        while (sectors > 0 && total_written < size) {
            uint32_t batch = sectors;
            if (batch > BLK_IO_MAX_SECTORS) batch = BLK_IO_MAX_SECTORS;

            /* 非对齐写入需要读-改-写 */
            if (sector_offset != 0 || (size - total_written) < batch * ss) {
                int ret = blk_ipc_read(ent->blk_ep, lba, batch, g_blk_buf);
                if (ret < 0)
                    return (total_written > 0) ? (int)total_written : ret;
            }

            uint32_t copy = batch * ss - sector_offset;
            if (copy > size - total_written) copy = size - total_written;
            memcpy(g_blk_buf + sector_offset, in + total_written, copy);

            int ret = blk_ipc_write(ent->blk_ep, lba, batch, g_blk_buf);
            if (ret < 0)
                return (total_written > 0) ? (int)total_written : ret;

            total_written += copy;
            lba += batch;
            sectors -= batch;
            sector_offset = 0;
        }

        return (int)total_written;
    }

    default:
        return -1;
    }
}

static int devfs_info(void *ctx, const char *path, struct vfs_info *info) {
    (void)ctx;

    if (path[0] == '/') path++;
    if (path[0] == '\0') {
        info->size = 0;
        info->type = VFS_TYPE_DIR;
        return 0;
    }

    struct dev_entry *ent = devfs_find(path);
    if (!ent) return -1;

    info->type = VFS_TYPE_FILE;
    if (ent->type == DEV_TYPE_BLOCK) {
        info->size = (uint32_t)(ent->part_sectors * ent->sector_size);
    } else {
        info->size = 0;
    }
    return 0;
}

static int devfs_finfo(void *ctx, uint32_t handle, struct vfs_info *info) {
    (void)ctx;

    if (handle >= DEVFS_MAX_FILES || !g_devfs.entries[handle].valid)
        return -1;

    struct dev_entry *ent = &g_devfs.entries[handle];
    info->type = VFS_TYPE_FILE;

    if (ent->type == DEV_TYPE_BLOCK) {
        info->size = (uint32_t)(ent->part_sectors * ent->sector_size);
    } else {
        info->size = 0;
    }
    return 0;
}

static int devfs_opendir(void *ctx, const char *path) {
    (void)ctx;
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) return 0;
    return -1;
}

static int devfs_readdir(void *ctx, uint32_t handle, uint32_t index,
                         struct vfs_dirent *entry) {
    (void)ctx;
    (void)handle;

    int count = 0;
    for (int i = 0; i < DEVFS_MAX_FILES; i++) {
        if (g_devfs.entries[i].valid) {
            if (count == (int)index) {
                strncpy(entry->name, g_devfs.entries[i].name,
                        sizeof(entry->name) - 1);
                entry->name[sizeof(entry->name) - 1] = '\0';
                entry->type = VFS_TYPE_FILE;
                return 0;
            }
            count++;
        }
    }
    return -1;
}

static int devfs_mkdir(void *ctx, const char *path) {
    (void)ctx; (void)path;
    return -1;
}

static int devfs_del(void *ctx, const char *path) {
    (void)ctx; (void)path;
    return -1;
}

static int devfs_truncate(void *ctx, uint32_t handle, uint64_t new_size) {
    (void)ctx; (void)handle; (void)new_size;
    return -1;
}

static int devfs_sync(void *ctx, uint32_t handle) {
    (void)ctx; (void)handle;
    return 0;
}

static int devfs_rename(void *ctx, const char *old_path,
                        const char *new_path) {
    (void)ctx; (void)old_path; (void)new_path;
    return -1;
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

/* ============== 消息处理 ============== */

/* 组合 handler: VFS (1-16) + DEVFS 注册 (200+) */
static int devfsd_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    if (op == UDM_DEVFS_REGISTER_BLOCK) {
        int result = devfs_handle_register_block(msg);
        msg->regs.data[0] = op;
        msg->regs.data[1] = (uint32_t)result;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        msg->handles.count = 0;
        return 0;
    }

    if (op == UDM_DEVFS_REGISTER_TTY) {
        int result = devfs_handle_register_tty(msg);
        msg->regs.data[0] = op;
        msg->regs.data[1] = (uint32_t)result;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        msg->handles.count = 0;
        return 0;
    }

    /* VFS OPEN: 对 TTY 设备返回 endpoint handle 和类型标记 */
    if (op == UDM_VFS_OPEN) {
        int ret = vfs_dispatch(&devfs_ops, &g_devfs, msg);
        int32_t handle = (int32_t)msg->regs.data[1];
        if (handle >= 0 && (uint32_t)handle < DEVFS_MAX_FILES) {
            struct dev_entry *ent = &g_devfs.entries[handle];
            if (ent->valid && ent->type == DEV_TYPE_TTY) {
                msg->regs.data[2] = DEVFS_TYPE_TTY;
                msg->handles.handles[0] = ent->blk_ep;
                msg->handles.count = 1;
            }
        }
        return ret;
    }

    return vfs_dispatch(&devfs_ops, &g_devfs, msg);
}

/* devfs 服务线程入口 */
static void *devfsd_thread(void *arg) {
    (void)arg;

    printf("[devfs] service thread started\n");

    struct sys_server srv = {
        .endpoint = g_devfs.endpoint,
        .handler  = devfsd_handler,
        .name     = "devfs",
    };

    sys_server_init(&srv);
    sys_server_run(&srv);

    printf("[devfs] service thread exiting\n");
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 初始化 devfs */
    memset(&g_devfs, 0, sizeof(g_devfs));

    /* 注册特殊设备 */
    devfs_register_special("null", DEV_TYPE_NULL);
    devfs_register_special("zero", DEV_TYPE_ZERO);

    /* 获取 endpoint (由 init 创建) */
    g_devfs.endpoint = env_require("devfs_ep");
    if (g_devfs.endpoint == HANDLE_INVALID) {
        printf("[devfs] FATAL: devfs_ep not found\n");
        return 1;
    }

    printf("[devfs] endpoint: %u\n", g_devfs.endpoint);

    /* 通知就绪 */
    svc_notify_ready("devfs");

    /* 运行服务线程 */
    pthread_t thread;
    if (pthread_create(&thread, NULL, devfsd_thread, NULL) != 0) {
        printf("[devfs] FATAL: failed to create thread\n");
        return 1;
    }

    pthread_join(thread, NULL);
    return 0;
}
