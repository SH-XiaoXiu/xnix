/**
 * @file kernel/boot/bootinfo.c
 * @brief Boot 资源信息收集
 *
 * 收集启动时的硬件资源信息,创建 boot handles 传给 init
 */

#include "boot_internal.h"

#include <arch/mmu.h>

#include <xnix/abi/handle.h>
#include <xnix/boot.h>
#include <xnix/physmem.h>
#include <xnix/stdio.h>
#include <xnix/types.h>

/**
 * Boot 资源描述符
 */
struct boot_resource {
    paddr_t  phys_addr; /* 物理地址 */
    uint32_t size;      /* 大小 */
    char     name[16];  /* 名称 */
};

/**
 * Boot 阶段收集的资源信息(物理地址和大小)
 */
static struct {
    struct boot_resource resources[CFG_MAX_BOOT_RESOURCES];
    uint32_t             count;
} g_boot_resources;

/**
 * 收集启动资源信息
 *
 * 在 boot_phase_late() 调用,仅记录物理地址和大小
 */
void boot_handles_collect(void) {
    g_boot_resources.count = 0;

    uint32_t mod_count = boot_get_module_count();
    pr_info("boot: found %u multiboot modules", mod_count);

    for (uint32_t i = 0; i < mod_count; i++) {
        void    *addr = NULL;
        uint32_t size = 0;

        if (boot_get_module(i, &addr, &size) < 0) {
            continue;
        }

        if (g_boot_resources.count >= CFG_MAX_BOOT_RESOURCES) {
            pr_err("bootinfo: too many boot resources");
            break;
        }

        struct boot_resource *res = &g_boot_resources.resources[g_boot_resources.count];
        res->phys_addr            = (paddr_t)(uintptr_t)addr;
        res->size                 = size;

        /* 从模块 cmdline 的 name= 参数获取名称,回退到索引 */
        const char *cmdline = boot_get_module_cmdline(i);
        if (!boot_kv_get_value(cmdline, "name", res->name, sizeof(res->name))) {
            snprintf(res->name, sizeof(res->name), "module%u", i);
        }

        pr_debug("boot: module %u: addr=0x%08x, size=%u, name=%s", i, res->phys_addr,
                 res->size, res->name);

        g_boot_resources.count++;
    }

    pr_info("boot: collected %u boot resources", g_boot_resources.count);
}

/**
 * 为 init 进程创建 boot handles
 *
 * 在 init 进程中直接创建硬件资源的 handles:
 * - framebuffer (fb_mem)
 * - 每个 Multiboot 模块 (module_<name>)
 *
 * 这个函数应该在 spawn_core 中 creator == NULL 时调用.
 *
 * @param proc init 进程
 * @return 0 成功, <0 失败
 */
int boot_handles_create_for_init(struct process *proc) {
    if (!proc) {
        return -1;
    }

    /* 创建 framebuffer handle */
    handle_t fb_handle = physmem_create_fb_handle_for_proc(proc, "fb_mem");
    if (fb_handle != HANDLE_INVALID) {
        pr_info("boot_handles: created fb_mem handle %u for init", fb_handle);
    }

    /* 为每个 Multiboot 模块创建 physmem handle */
    for (uint32_t i = 0; i < g_boot_resources.count; i++) {
        struct boot_resource *res = &g_boot_resources.resources[i];

        /* 构建 handle 名称: "module_<name>" */
        char handle_name[32];
        snprintf(handle_name, sizeof(handle_name), "module_%s", res->name);

        handle_t h = physmem_create_handle_for_proc(proc, res->phys_addr, res->size, handle_name);
        if (h != HANDLE_INVALID) {
            pr_info("boot_handles: created %s handle %u (%u bytes)", handle_name, h, res->size);
        }
    }

    return 0;
}
