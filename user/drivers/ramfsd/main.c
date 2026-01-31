/**
 * @file main.c
 * @brief ramfsd 驱动程序入口
 */

#include "ramfs.h"

#include <stdio.h>
#include <udm/server.h>
#include <vfs/vfs.h>

#define BOOT_VFS_EP 0

static struct ramfs_ctx g_ramfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(ramfs_get_ops(), &g_ramfs, msg);
}

int main(void) {
    printf("[ramfsd] Starting RAM filesystem driver\n");

    ramfs_init(&g_ramfs);

    struct udm_server srv = {
        .endpoint = BOOT_VFS_EP,
        .handler  = vfs_handler,
        .name     = "ramfsd",
    };

    udm_server_init(&srv);
    printf("[ramfsd] Ready, serving on endpoint %d\n", BOOT_VFS_EP);
    udm_server_run(&srv);

    return 0;
}
