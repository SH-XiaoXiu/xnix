/**
 * @file main.c
 * @brief ramfsd 驱动程序入口
 */

#include "ramfs.h"

#include <d/server.h>
#include <stdio.h>
#include <vfs/vfs.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

static struct ramfs_ctx g_ramfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(ramfs_get_ops(), &g_ramfs, msg);
}

int main(void) {
    /* 获取 endpoint handle (ramfsd provides ramfs_ep) */
    handle_t ep = env_get_handle("ramfs_ep");
    if (ep == HANDLE_INVALID) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[ramfsd]", " Failed to find ramfs_ep handle\n");
        return 1;
    }

    /* serial_ep 由 init 传递 (requires serial_ep) */
    handle_t serial_ep = env_get_handle("serial_ep");
    (void)serial_ep;

    ramfs_init(&g_ramfs);

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = vfs_handler,
        .name     = "ramfsd",
    };

    udm_server_init(&srv);
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[ramfsd]", " Ready, serving on endpoint %u\n", ep);

    /* 通知 init 服务已就绪 */
    svc_notify_ready("ramfsd");

    udm_server_run(&srv);

    return 0;
}
