/**
 * @file main.c
 * @brief fatfsd 驱动程序入口
 *
 * FAT 文件系统用户态驱动, 支持两种后端:
 * - 内存模式: boot.system handle 存在时, mmap 该模块
 * - ATA 模式: 否则初始化 ATA, 读 MBR 分区表, 挂载第一个分区
 */

#include "ata.h"
#include "diskio_mem.h"
#include "fatfs_vfs.h"

#include <block.h>
#include <pthread.h>
#include <xnix/protocol/blk.h>
#include <xnix/protocol/vfs.h>
#include <d/server.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vfs/vfs.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define MBR_PART_TABLE_OFFSET 446
#define MBR_SIGNATURE         0xAA55

struct mbr_partition {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed));

static struct fatfs_ctx g_fatfs;

/* ============== 块设备适配器 ============== */

/* ATA 驱动上下文 */
struct ata_block_ctx {
    int drive;
    uint32_t sector_count;
};

static struct ata_block_ctx g_ata_ctx[2];
static struct block_device g_block_dev[2];
static bool g_block_dev_registered[2] = {false, false};

/* 块设备读取操作 */
static int block_ata_read(void *ctx, uint64_t lba, uint32_t count, void *buffer) {
    struct ata_block_ctx *actx = (struct ata_block_ctx *)ctx;
    if (!ata_is_ready(actx->drive)) {
        return -1;
    }
    return ata_read(actx->drive, (uint32_t)lba, count, buffer);
}

/* 块设备写入操作 */
static int block_ata_write(void *ctx, uint64_t lba, uint32_t count, const void *buffer) {
    struct ata_block_ctx *actx = (struct ata_block_ctx *)ctx;
    if (!ata_is_ready(actx->drive)) {
        return -1;
    }
    return ata_write(actx->drive, (uint32_t)lba, count, buffer);
}

/* 块设备刷新操作 */
static int block_ata_flush(void *ctx) {
    (void)ctx;
    /* ATA 驱动在每个扇区写入后自动刷新 */
    return 0;
}

/* 块设备信息获取操作 */
static int block_ata_get_info(void *ctx, struct block_info *info) {
    struct ata_block_ctx *actx = (struct ata_block_ctx *)ctx;
    info->sector_count = actx->sector_count;
    info->sector_size = 512;
    info->type = BLOCK_DEV_ATA;
    strncpy(info->model, "ATA Disk", sizeof(info->model) - 1);
    info->serial[0] = '\0';
    return 0;
}

/* ATA 块设备操作表 */
static struct block_ops ata_block_ops = {
    .read = block_ata_read,
    .write = block_ata_write,
    .flush = block_ata_flush,
    .get_info = block_ata_get_info,
};

/**
 * 注册 ATA 块设备
 * @param drive ATA 驱动器号 (0 或 1)
 */
static void register_ata_block_device(int drive) {
    if (drive < 0 || drive > 1) return;
    if (g_block_dev_registered[drive]) return;

    if (!ata_is_ready(drive)) {
        return;
    }

    uint32_t sectors = ata_get_sector_count(drive);
    if (sectors == 0) {
        return;
    }

    /* 初始化上下文 */
    g_ata_ctx[drive].drive = drive;
    g_ata_ctx[drive].sector_count = sectors;

    /* 初始化块设备 */
    memset(&g_block_dev[drive], 0, sizeof(g_block_dev[drive]));
    g_block_dev[drive].type = BLOCK_DEV_ATA;
    g_block_dev[drive].ops = &ata_block_ops;
    g_block_dev[drive].driver_ctx = &g_ata_ctx[drive];
    g_block_dev[drive].info.sector_count = sectors;
    g_block_dev[drive].info.sector_size = 512;
    g_block_dev[drive].info.type = BLOCK_DEV_ATA;

    /* 注册设备（名称自动分配） */
    if (block_register(&g_block_dev[drive]) == 0) {
        g_block_dev_registered[drive] = true;
        ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]",
                  " registered block device: %s (%u MB)\n",
                  g_block_dev[drive].name,
                  sectors / 2048);
    }
}

/**
 * 从 MBR 解析第一个有效分区的起始 LBA
 */
static int parse_mbr_first_partition(const uint8_t *mbr, uint32_t *out_lba) {
    uint16_t sig = (uint16_t)mbr[510] | ((uint16_t)mbr[511] << 8);
    if (sig != MBR_SIGNATURE) {
        return -1;
    }

    const struct mbr_partition *parts = (const struct mbr_partition *)(mbr + MBR_PART_TABLE_OFFSET);

    for (int i = 0; i < 4; i++) {
        if (parts[i].type != 0 && parts[i].lba_start != 0) {
            *out_lba = parts[i].lba_start;
            return 0;
        }
    }

    return -1;
}

/* ============== BLK IPC 处理 ============== */

static int  g_blk_ata_drive = -1; /* ATA 模式时保存的驱动器号 */
static char g_blk_dev_name[16];   /* 块设备名 (e.g., "sda") */
static uint32_t g_blk_sector_count;
static uint8_t g_saved_mbr[512];  /* main() 阶段读取的 MBR, 注册时复用 */

static char g_blk_io_buf[BLK_IO_BUF_SIZE];

static int blk_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    if (g_blk_ata_drive < 0) {
        msg->regs.data[1] = (uint32_t)-6; /* EIO */
        return 0;
    }

    switch (op) {
    case UDM_BLK_READ: {
        uint64_t lba = (uint64_t)msg->regs.data[2] << 32 | msg->regs.data[1];
        uint32_t count = msg->regs.data[3];
        if (count > BLK_IO_MAX_SECTORS) count = BLK_IO_MAX_SECTORS;

        int ret = ata_read(g_blk_ata_drive, (uint32_t)lba, count, g_blk_io_buf);
        if (ret < 0) {
            msg->regs.data[1] = (uint32_t)-6;
            return 0;
        }

        uint32_t bytes = count * 512;
        msg->regs.data[1] = bytes;
        msg->buffer.data = (uint64_t)(uintptr_t)g_blk_io_buf;
        msg->buffer.size = bytes;
        return 0;
    }

    case UDM_BLK_WRITE: {
        uint64_t lba = (uint64_t)msg->regs.data[2] << 32 | msg->regs.data[1];
        uint32_t count = msg->regs.data[3];
        if (count > BLK_IO_MAX_SECTORS) count = BLK_IO_MAX_SECTORS;

        if (!msg->buffer.data || msg->buffer.size < count * 512) {
            msg->regs.data[1] = (uint32_t)-22; /* EINVAL */
            return 0;
        }

        memcpy(g_blk_io_buf, (const void *)(uintptr_t)msg->buffer.data, count * 512);
        int ret = ata_write(g_blk_ata_drive, (uint32_t)lba, count, g_blk_io_buf);
        if (ret < 0) {
            msg->regs.data[1] = (uint32_t)-6;
            return 0;
        }

        msg->regs.data[1] = count * 512;
        return 0;
    }

    case UDM_BLK_INFO: {
        msg->regs.data[1] = 0;
        msg->regs.data[2] = (uint32_t)g_blk_sector_count;
        msg->regs.data[3] = 0;
        msg->regs.data[4] = 512;
        msg->buffer.data = (uint64_t)(uintptr_t)g_blk_dev_name;
        msg->buffer.size = (uint32_t)strlen(g_blk_dev_name);
        return 0;
    }

    default:
        msg->regs.data[1] = (uint32_t)-38; /* ENOSYS */
        return 0;
    }
}

/* 组合 handler: VFS + BLK */
static int combined_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);
    /* BLK 协议: 100-199, IO 协议: 0x100+ (256+), VFS 协议: 0-99 */
    if (op >= 100 && op < 0x100) return blk_handler(msg);
    return vfs_dispatch(fatfs_get_ops(), &g_fatfs, msg);
}

/* 向 devfsd 注册块设备 */
/* 注册缓冲区: [name\0][mbr 512B] */
static char g_reg_buf[16 + 1 + 512];

static void register_blkdev_to_devfsd(handle_t self_ep) {
    if (g_blk_ata_drive < 0 || g_blk_dev_name[0] == '\0') return;

    /* 重试查找 devfs_ep */
    handle_t devfs = HANDLE_INVALID;
    for (int i = 0; i < 30; i++) {
        devfs = sys_handle_find("devfs_ep");
        if (devfs != HANDLE_INVALID) break;
        sys_sleep(100);
    }
    if (devfs == HANDLE_INVALID) return;

    /* 构建 buffer: name\0 + MBR (复用 main 阶段保存的 MBR) */
    uint32_t name_len = (uint32_t)strlen(g_blk_dev_name);
    memcpy(g_reg_buf, g_blk_dev_name, name_len);
    g_reg_buf[name_len] = '\0';
    memcpy(g_reg_buf + name_len + 1, g_saved_mbr, 512);

    struct ipc_message reg   = {0};
    struct ipc_message reply = {0};

    reg.regs.data[0] = UDM_DEVFS_REGISTER_BLOCK;
    reg.regs.data[1] = g_blk_sector_count;
    reg.regs.data[2] = 0;
    reg.regs.data[3] = 512;
    reg.regs.data[4] = name_len;
    reg.buffer.data = (uint64_t)(uintptr_t)g_reg_buf;
    reg.buffer.size = name_len + 1 + 512;
    reg.handles.handles[0] = self_ep;
    reg.handles.count = 1;

    sys_ipc_call(devfs, &reg, &reply, 5000);
}

static void *srv_thread_entry(void *arg) {
    udm_server_run((struct udm_server *)arg);
    return NULL;
}

int main(int argc, char **argv) {
    /* 解析参数: --ata 强制 ATA 模式，--drive N 指定 ATA 驱动器号 */
    bool force_ata = false;
    int  ata_drive = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ata") == 0) {
            force_ata = true;
        } else if (strcmp(argv[i], "--drive") == 0 && i + 1 < argc) {
            ata_drive = (int)strtol(argv[i + 1], NULL, 10);
            i++;
        }
    }

    /* 根据驱动器号生成服务名和端点名（drive 0 保持向后兼容名称） */
    char ep_name_buf[32];
    char svc_name_buf[32];
    const char *ep_name;
    const char *svc_name;
    if (force_ata) {
        if (ata_drive == 0) {
            ep_name  = "fatfs_ata_ep";
            svc_name = "fatfs_ata";
        } else {
            snprintf(ep_name_buf, sizeof(ep_name_buf), "fatfs_ata%d_ep", ata_drive);
            snprintf(svc_name_buf, sizeof(svc_name_buf), "fatfs_ata%d", ata_drive);
            ep_name  = ep_name_buf;
            svc_name = svc_name_buf;
        }
    } else {
        ep_name  = "fatfs_ep";
        svc_name = "fatfs";
    }

    env_set_name("fatfs");
    handle_t ep = env_require(ep_name);
    if (ep == HANDLE_INVALID) {
        return 1;
    }

    bool use_ata = force_ata;

    if (!force_ata) {
        /* 自动检测: boot.system 存在则为内存模式 */
        handle_t system_h = sys_handle_find("boot.system");
        if (system_h != HANDLE_INVALID) {
            uint32_t system_size = 0;
            void    *system_addr = sys_mmap_phys(system_h, 0, 0, 0x03, &system_size);
            if (system_addr == NULL || (intptr_t)system_addr < 0) {
                ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfs]",
                          " failed to mmap boot.system\n");
                return 1;
            }

            disk_init_memory(system_addr, system_size);
            ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfs]", " memory mode (size=%u)\n",
                      system_size);
        } else {
            use_ata = true;
        }
    }

    if (use_ata) {
        if (ata_init() < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfs]", " ata init failed\n");
            return 1;
        }

        if (ata_read(ata_drive, 0, 1, g_saved_mbr) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfs]", " failed to read MBR (drive=%d)\n",
                      ata_drive);
            return 1;
        }
        uint8_t *mbr = g_saved_mbr;

        uint32_t base_lba = 0;
        if (parse_mbr_first_partition(mbr, &base_lba) < 0) {
            /* 无有效 MBR, 视为裸磁盘 (无分区表, base_lba=0) */
            base_lba = 0;
        }

        disk_init_ata(ata_drive, base_lba);
        ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfs]",
                  " ATA mode (drive=%d, base_lba=%u)\n", ata_drive, base_lba);

        /* 记录 ATA 信息供 BLK 协议使用 */
        g_blk_ata_drive = ata_drive;
        g_blk_sector_count = ata_get_sector_count(ata_drive);
        /* 注册当前驱动器的块设备 */
        register_ata_block_device(ata_drive);
        /* 设备名: drive 0 → sda, drive 1 → sdb, ... */
        snprintf(g_blk_dev_name, sizeof(g_blk_dev_name), "sd%c",
                 'a' + ata_drive);
    }

    /* 初始化 FatFs */
    if (fatfs_init(&g_fatfs) < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfs]", " fatfs init failed\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = combined_handler,
        .name     = svc_name,
    };

    udm_server_init(&srv);
    svc_notify_ready(svc_name);
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfs]", " %s started\n", svc_name);

    /* ATA 模式: 先启动服务线程, 再注册到 devfsd */
    if (use_ata) {
        pthread_t srv_thread;
        pthread_create(&srv_thread, NULL, srv_thread_entry, &srv);
        sys_sleep(50); /* 等待服务线程进入 receive 循环 */
        register_blkdev_to_devfsd(ep);
        pthread_join(srv_thread, NULL);
    } else {
        udm_server_run(&srv);
    }

    return 0;
}
