/**
 * @file main.c
 * @brief fatfsd 驱动程序入口
 *
 * FAT 文件系统用户态驱动, 支持两种后端:
 * - 内存模式: module_system handle 存在时, mmap 该模块
 * - ATA 模式: 否则初始化 ATA, 读 MBR 分区表, 挂载第一个分区
 */

#include "ata.h"
#include "diskio_mem.h"
#include "fatfs_vfs.h"

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

int main(void) {
    handle_t ep = env_get_handle("fatfs_ep");
    if (ep == HANDLE_INVALID) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " failed to find fatfs_ep handle\n");
        return 1;
    }

    /* 检测模式: module_system 存在则为内存模式 */
    handle_t system_h = sys_handle_find("module_system");
    if (system_h != HANDLE_INVALID) {
        /* 内存模式 */
        uint32_t system_size = 0;
        void    *system_addr = sys_mmap_phys(system_h, 0, 0, 0x03, &system_size);
        if (system_addr == NULL || (intptr_t)system_addr < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " failed to mmap module_system\n");
            return 1;
        }

        disk_init_memory(system_addr, system_size);
        ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]", " memory mode (size=%u)\n",
                  system_size);
    } else {
        /* ATA 模式 */
        if (ata_init() < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " ata init failed\n");
            return 1;
        }

        /* 读取 MBR */
        uint8_t mbr[512];
        if (ata_read(0, 0, 1, mbr) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " failed to read MBR\n");
            return 1;
        }

        uint32_t base_lba = 0;
        if (parse_mbr_first_partition(mbr, &base_lba) < 0) {
            ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " no valid MBR partition found\n");
            return 1;
        }

        disk_init_ata(0, base_lba);
        ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]",
                  " ATA mode (drive=0, partition LBA=%u)\n", base_lba);
    }

    /* 初始化 FatFs */
    if (fatfs_init(&g_fatfs) < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " fatfs init failed\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = vfs_handler,
        .name     = "fatfsd",
    };

    udm_server_init(&srv);
    svc_notify_ready("fatfsd");
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[fatfsd]", " started\n");

    udm_server_run(&srv);

    return 0;
}
