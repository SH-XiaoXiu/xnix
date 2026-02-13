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

    handle_t ep = env_get_handle(ep_name);
    if (ep == HANDLE_INVALID) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[fatfsd]", " failed to find %s handle\n", ep_name);
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
