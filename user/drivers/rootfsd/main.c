/**
 * @file main.c
 * @brief rootfsd 驱动程序入口
 *
 * 根文件系统驱动,从内存中的 FAT 镜像提供文件系统服务.
 * rootfs.img 作为 Multiboot module 加载,通过 HANDLE_PHYSMEM 访问.
 */

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

/* diskio 设置函数(在 diskio.c 中定义) */
extern void diskio_set_image(const void *image, uint32_t size);

static struct fatfs_ctx g_fatfs;

static int vfs_handler(struct ipc_message *msg) {
    return vfs_dispatch(fatfs_get_ops(), &g_fatfs, msg);
}

/* Debug output via syscall (doesn't need handles) */
static void debug_puts(const char *s) {
    while (*s) {
        __asm__ volatile("int $0x80" : : "a"(901), "b"(*s));
        s++;
    }
}

int main(void) {
    debug_puts("[rootfsd] DEBUG: main() entered\n");
    printf("[rootfsd] Starting root filesystem driver\n");

    /* 获取 endpoint handle (rootfsd provides rootfs_ep) */
    handle_t ep = env_get_handle("rootfs_ep");
    if (ep == HANDLE_INVALID) {
        printf("[rootfsd] Failed to find rootfs_ep handle\n");
        return 1;
    }
    printf("[rootfsd] Using endpoint handle %u for 'rootfs_ep'\n", ep);

    /* 查找 rootfs module 的 physmem handle */
    debug_puts("[rootfsd] DEBUG: looking for module_rootfs\n");
    handle_t mod_handle = sys_handle_find("module_rootfs");
    if (mod_handle == HANDLE_INVALID) {
        debug_puts("[rootfsd] DEBUG: module_rootfs NOT FOUND!\n");
        printf("[rootfsd] Failed to find module_rootfs handle\n");
        return 1;
    }
    debug_puts("[rootfsd] DEBUG: module_rootfs found\n");
    printf("[rootfsd] Found module_rootfs handle: %u\n", mod_handle);

    /* 使用 sys_mmap_phys 映射模块到用户空间 */
    uint32_t mod_size = 0;
    void *mod_addr = sys_mmap_phys(mod_handle, 0, 0, 0x03, &mod_size); /* PROT_READ | PROT_WRITE */
    if (mod_addr == (void *)-1 || mod_addr == NULL) {
        printf("[rootfsd] Failed to map module\n");
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
