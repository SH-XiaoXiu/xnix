/**
 * @file main.c
 * @brief ramfsd 驱动程序入口
 */

#include "ramfs.h"

#include <d/server.h>
#include <stdio.h>
#include <vfs/vfs.h>

#define RAMFS_EP_HANDLE 3 /* 从 service.conf caps = ramfs_ep:3 */

static struct ramfs_ctx g_ramfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(ramfs_get_ops(), &g_ramfs, msg);
}

int main(void) {
    printf("[ramfsd] Starting RAM filesystem driver\n");

    ramfs_init(&g_ramfs);

    struct udm_server srv = {
        .endpoint = RAMFS_EP_HANDLE,
        .handler  = vfs_handler,
        .name     = "ramfsd",
    };

    udm_server_init(&srv);
    printf("[ramfsd] Ready, serving on endpoint %d\n", RAMFS_EP_HANDLE);
    udm_server_run(&srv);

    return 0;
}
