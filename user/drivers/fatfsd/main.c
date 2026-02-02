/**
 * @file main.c
 * @brief fatfsd 驱动程序入口
 *
 * FAT 文件系统用户态驱动,通过 ATA PIO 访问磁盘.
 */

#include "ata.h"
#include "fatfs_vfs.h"

#include <stdio.h>
#include <udm/server.h>
#include <vfs/vfs.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

#define BOOT_VFS_EP       0
#define BOOT_ATA_IO_CAP   1
#define BOOT_ATA_CTRL_CAP 2

static struct fatfs_ctx g_fatfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(fatfs_get_ops(), &g_fatfs, msg);
}

int main(void) {
    printf("[fatfsd] Starting FAT filesystem driver\n");

    /* 初始化 ATA 驱动 */
    if (ata_init(BOOT_ATA_IO_CAP, BOOT_ATA_CTRL_CAP) < 0) {
        printf("[fatfsd] Failed to initialize ATA driver\n");
        return 1;
    }

    /* 初始化 FatFs */
    if (fatfs_init(&g_fatfs) < 0) {
        printf("[fatfsd] Failed to initialize FatFs\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = BOOT_VFS_EP,
        .handler  = vfs_handler,
        .name     = "fatfsd",
    };

    udm_server_init(&srv);

    /* 创建 ready 文件通知 init 服务已就绪 */
    sys_mkdir("/run");
    int fd = sys_open("/run/fatfsd.ready", VFS_O_CREAT | VFS_O_WRONLY);
    if (fd >= 0) {
        sys_close(fd);
    }

    printf("[fatfsd] Ready, serving on endpoint %d\n", BOOT_VFS_EP);
    udm_server_run(&srv);

    return 0;
}
