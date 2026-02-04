/**
 * @file main.c
 * @brief rootfsd 驱动程序入口
 *
 * 根文件系统驱动,从内存中的 FAT 镜像提供文件系统服务.
 * rootfs.img 作为 Multiboot module 加载.
 */

#include "fatfs_vfs.h"

#include <d/protocol/vfs.h>
#include <d/server.h>
#include <module_index.h>
#include <stdio.h>
#include <string.h>
#include <vfs/vfs.h>
#include <vfs_client.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

/* diskio 设置函数(在 diskio.c 中定义) */
extern void diskio_set_image(const void *image, uint32_t size);

static struct fatfs_ctx g_fatfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(fatfs_get_ops(), &g_fatfs, msg);
}

/**
 * 简单的字符串转整数
 */
static int simple_atoi(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/**
 * 解析启动参数获取 module 索引
 * 格式: module=N
 * 默认返回 MODULE_ROOTFS (4)
 */
static int parse_module_index(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "module=", 7) == 0) {
            return simple_atoi(argv[i] + 7);
        }
    }
    return MODULE_ROOTFS; /* 默认使用固定的 rootfs.img 模块索引 */
}

int main(int argc, char **argv) {
    printf("[rootfsd] Starting root filesystem driver\n");

    /* 使用 init 传递的 endpoint handle (rootfsd provides rootfs_ep) */
    /* Init 传递的第一个 handle 是自己提供的 endpoint */
    handle_t ep = 0; /* Slot 0: rootfs_ep (provides) */
    printf("[rootfsd] Using endpoint handle %u for 'rootfs_ep'\n", ep);

    /* ramfs_ep 由 init 传递 (requires ramfs_ep) */
    handle_t ramfs_ep = 1; /* Slot 1: ramfs_ep (requires) */
    printf("[rootfsd] Received 'ramfs_ep' handle: %u\n", ramfs_ep);

    /* 解析 module 索引 */
    int module_index = parse_module_index(argc, argv);

    printf("[rootfsd] Mapping module %d\n", module_index);

    /* 映射 rootfs module 到用户空间 */
    uint32_t mod_size = 0;
    void    *mod_addr = sys_module_map((uint32_t)module_index, &mod_size);
    if (mod_addr == (void *)-1 || mod_addr == NULL) {
        printf("[rootfsd] Failed to map module %d\n", module_index);
        return 1;
    }

    printf("[rootfsd] Module mapped at %p, size %u bytes\n", mod_addr, mod_size);

    /* 设置 FAT 镜像 */
    diskio_set_image(mod_addr, mod_size);

    /* 初始化 FatFs */
    if (fatfs_init(&g_fatfs) < 0) {
        printf("[rootfsd] Failed to initialize FatFs\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = vfs_handler,
        .name     = "rootfsd",
    };

    udm_server_init(&srv);

    printf("[rootfsd] Ready, serving on endpoint %u\n", ep);

    /* 通知 init 服务已就绪 */
    svc_notify_ready("rootfsd");

    udm_server_run(&srv);

    return 0;
}
