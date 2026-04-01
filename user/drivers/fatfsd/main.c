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
#include <d/protocol/vfs.h>
#include <d/server.h>
#include <stdio.h>
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

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(fatfs_get_ops(), &g_fatfs, msg);
}

int main(int argc, char **argv) {
    /* 解析参数: --ata 强制 ATA 模式 */
    bool force_ata = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--ata") == 0) {
            force_ata = true;
        }
    }

    const char *ep_name  = force_ata ? "fatfs_ata_ep" : "fatfs_ep";
    const char *svc_name = force_ata ? "fatfsd_ata" : "fatfsd";

    env_set_name("fatfsd");
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
                ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]",
                          " failed to mmap boot.system\n");
                return 1;
            }

            disk_init_memory(system_addr, system_size);
            ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]", " memory mode (size=%u)\n",
                      system_size);
        } else {
            use_ata = true;
        }
    }

    if (use_ata) {
        if (ata_init() < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " ata init failed\n");
            return 1;
        }

        uint8_t mbr[512];
        if (ata_read(0, 0, 1, mbr) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " failed to read MBR\n");
            return 1;
        }

        uint32_t base_lba = 0;
        if (parse_mbr_first_partition(mbr, &base_lba) < 0) {
            /* 无有效 MBR, 视为裸磁盘 (无分区表, base_lba=0) */
            base_lba = 0;
        }

        disk_init_ata(0, base_lba);
        ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]", " ATA mode (drive=0, base_lba=%u)\n",
                  base_lba);

        /* 注册块设备（供 devfsd 使用） */
        register_ata_block_device(0);
        /* 如果有从盘，也注册 */
        if (ata_is_ready(1)) {
            register_ata_block_device(1);
        }
    }

    /* 初始化 FatFs */
    if (fatfs_init(&g_fatfs) < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " fatfs init failed\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = vfs_handler,
        .name     = svc_name,
    };

    udm_server_init(&srv);
    svc_notify_ready(svc_name);
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]", " %s started\n", svc_name);

    udm_server_run(&srv);

    return 0;
}
