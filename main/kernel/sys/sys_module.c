/**
 * @file kernel/sys/sys_module.c
 * @brief Boot module 系统调用实现
 */

#include <kernel/process/process.h>
#include <kernel/sys/syscall.h>
#include <xnix/abi/syscall.h>
#include <xnix/boot.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/usraccess.h>
#include <xnix/vmm.h>

/* Module 映射基地址(在用户空间高地址区域) */
#define MODULE_MAP_BASE 0x30000000

/**
 * SYS_MODULE_MAP: 映射 boot module 到用户空间
 *
 * @param args[0] index module 索引
 * @param args[1] size_out 输出 module 大小的用户空间指针(可为 NULL)
 * @return 用户空间映射地址,失败返回 -1
 */
static int32_t sys_module_map(const uint32_t *args) {
    uint32_t  index    = args[0];
    uint32_t *size_out = (uint32_t *)args[1];

    struct process *proc = process_get_current();
    if (!proc) {
        return -ESRCH;
    }

    /* 获取 module 信息 */
    void    *mod_addr = NULL;
    uint32_t mod_size = 0;
    if (boot_get_module(index, &mod_addr, &mod_size) < 0) {
        return -ENOENT;
    }

    if (mod_size == 0) {
        return -ENOENT;
    }

    /* 物理地址(boot_get_module 返回的是物理地址) */
    uint32_t mod_phys = (uint32_t)(uintptr_t)mod_addr;

    /* 页对齐 */
    uint32_t mod_phys_start = mod_phys & ~(PAGE_SIZE - 1);
    uint32_t mod_offset     = mod_phys - mod_phys_start;
    uint32_t mod_pages      = (mod_size + mod_offset + PAGE_SIZE - 1) / PAGE_SIZE;

    /* 用户空间映射基地址(每个 module 分配不同区域) */
    uint32_t user_base = MODULE_MAP_BASE + index * 0x1000000; /* 每个 module 最多 16MB */

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->map) {
        return -ENODEV;
    }

    /* 映射所有 module 页面(只读) */
    for (uint32_t i = 0; i < mod_pages; i++) {
        uint32_t vaddr = user_base + i * PAGE_SIZE;
        uint32_t paddr = mod_phys_start + i * PAGE_SIZE;

        int ret = mm->map(proc->page_dir_phys, vaddr, paddr, VMM_PROT_USER | VMM_PROT_READ);
        if (ret != 0) {
            /* 回滚已映射的页面 */
            for (uint32_t j = 0; j < i; j++) {
                mm->unmap(proc->page_dir_phys, user_base + j * PAGE_SIZE);
            }
            return -ENOMEM;
        }
    }

    /* 输出 module 大小 */
    if (size_out) {
        if (copy_to_user(size_out, &mod_size, sizeof(mod_size)) != 0) {
            /* 映射成功但大小输出失败,不回滚 */
        }
    }

    /* 返回用户空间地址(包含页内偏移) */
    return (int32_t)(user_base + mod_offset);
}

/**
 * 注册 module 系统调用
 */
void sys_module_init(void) {
    syscall_register(SYS_MODULE_MAP, sys_module_map, 2, "module_map");
}
