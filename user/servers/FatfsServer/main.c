/**
 * @file main.c
 * @brief fatfsd 驱动程序入口
 *
 * FAT 文件系统用户态驱动,通过 ATA PIO 访问磁盘.
 */

#include "ata.h"
#include "fatfs_vfs.h"

#include <d/protocol/vfs.h>
#include <d/server.h>
#include <stdio.h>
#include <vfs/vfs.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

#define BOOT_VFS_EP       0
#define BOOT_ATA_IO_CAP   1
#define BOOT_ATA_CTRL_CAP 2

static struct fatfs_ctx g_fatfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(fatfs_get_ops(), &g_fatfs, msg);
}

int main(void) {
    /* 初始化 ATA 驱动 */
    if (ata_init(BOOT_ATA_IO_CAP, BOOT_ATA_CTRL_CAP) < 0) {
        printf("[fatfsd] ata init failed\n");
        return 1;
    }

    /* 初始化 FatFs */
    if (fatfs_init(&g_fatfs) < 0) {
        printf("[fatfsd] fatfs init failed\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = BOOT_VFS_EP,
        .handler  = vfs_handler,
        .name     = "fatfsd",
    };

    udm_server_init(&srv);
    printf("[fatfsd] started\n");

    udm_server_run(&srv);

    return 0;
}
