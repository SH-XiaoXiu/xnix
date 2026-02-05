/**
 * @file main.c
 * @brief ramfsd 驱动程序入口
 */

#include "ramfs.h"

#include <d/server.h>
#include <stdio.h>
#include <vfs/vfs.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

static struct ramfs_ctx g_ramfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(ramfs_get_ops(), &g_ramfs, msg);
}

int main(void) {
    printf("[ramfsd] Starting RAM filesystem driver\n");

    /* 使用 init 传递的 endpoint handle (ramfsd provides ramfs_ep) */
    /* Init 传递的第一个 handle 是自己提供的 endpoint */
    handle_t ep = 0; /* Slot 0: ramfs_ep (provides) */
    printf("[ramfsd] Using endpoint handle %u for 'ramfs_ep'\n", ep);

    /* serial_ep 由 init 传递 (requires serial_ep) */
    handle_t serial_ep = 1; /* Slot 1: serial_ep (requires) */
    printf("[ramfsd] Received 'serial_ep' handle: %u\n", serial_ep);

    ramfs_init(&g_ramfs);

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = vfs_handler,
        .name     = "ramfsd",
    };

    udm_server_init(&srv);
    printf("[ramfsd] Ready, serving on endpoint %u\n", ep);

    /* 通知 init 服务已就绪 */
    svc_notify_ready("ramfsd");

    udm_server_run(&srv);

    return 0;
}
