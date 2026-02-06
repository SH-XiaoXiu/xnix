/**
 * @file physmem.c
 * @brief 物理内存区域 (HANDLE_PHYSMEM) 实现
 */

#include <kernel/process/process.h>
#include <xnix/abi/framebuffer.h>
#include <xnix/boot.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/physmem.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/vmm.h>

struct physmem_region *physmem_create(paddr_t phys_addr, uint32_t size, physmem_type_t type) {
    struct physmem_region *region = kmalloc(sizeof(struct physmem_region));
    if (!region) {
        return NULL;
    }

    memset(region, 0, sizeof(*region));
    region->phys_addr = phys_addr;
    region->size      = size;
    region->type      = type;
    region->refcount  = 1;

    return region;
}

void physmem_get(struct physmem_region *region) {
    if (region) {
        region->refcount++;
    }
}

void physmem_put(struct physmem_region *region) {
    if (region && --region->refcount == 0) {
        kfree(region);
    }
}

handle_t physmem_create_handle_for_proc(struct process *proc, paddr_t phys_addr, uint32_t size,
                                        const char *name) {
    struct physmem_region *region = physmem_create(phys_addr, size, PHYSMEM_TYPE_GENERIC);
    if (!region) {
        return HANDLE_INVALID;
    }

    handle_t h = handle_alloc(proc, HANDLE_PHYSMEM, region, name);
    if (h == HANDLE_INVALID) {
        physmem_put(region);
        return HANDLE_INVALID;
    }

    return h;
}

handle_t physmem_create_fb_handle_for_proc(struct process *proc, const char *name) {
    struct boot_framebuffer_info fb;
    if (boot_get_framebuffer(&fb) < 0) {
        pr_warn("physmem: no framebuffer available");
        return HANDLE_INVALID;
    }

    /* 计算 framebuffer 大小 */
    uint32_t fb_size = fb.pitch * fb.height;

    struct physmem_region *region = physmem_create((paddr_t)fb.addr, fb_size, PHYSMEM_TYPE_FB);
    if (!region) {
        return HANDLE_INVALID;
    }

    /* 填充 FB 元数据 */
    region->fb_info.width      = fb.width;
    region->fb_info.height     = fb.height;
    region->fb_info.pitch      = fb.pitch;
    region->fb_info.bpp        = fb.bpp;
    region->fb_info.red_pos    = fb.red_pos;
    region->fb_info.red_size   = fb.red_size;
    region->fb_info.green_pos  = fb.green_pos;
    region->fb_info.green_size = fb.green_size;
    region->fb_info.blue_pos   = fb.blue_pos;
    region->fb_info.blue_size  = fb.blue_size;

    handle_t h = handle_alloc(proc, HANDLE_PHYSMEM, region, name);
    if (h == HANDLE_INVALID) {
        physmem_put(region);
        return HANDLE_INVALID;
    }

    pr_debug("physmem: created fb handle %u for proc '%s': %ux%u @ 0x%08x", h, proc->name, fb.width,
             fb.height, (uint32_t)fb.addr);

    return h;
}

uint32_t physmem_map_to_user(struct process *proc, struct physmem_region *region, uint32_t offset,
                             uint32_t size, uint32_t prot) {
    if (!proc || !region) {
        return 0;
    }

    /* 验证偏移和大小 */
    if (offset >= region->size) {
        return 0;
    }
    if (size == 0 || offset + size > region->size) {
        size = region->size - offset;
    }

    /* 计算映射的页数 */
    uint32_t start_page = offset & ~(PAGE_SIZE - 1);
    uint32_t end_offset = offset + size;
    uint32_t end_page   = (end_offset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t num_pages  = (end_page - start_page) / PAGE_SIZE;

    /* 选择用户空间映射基地址 */
    uint32_t user_base = ABI_FB_MAP_BASE; /* 固定地址,简化实现 */

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->map) {
        pr_err("physmem: no mm operations available");
        return 0;
    }

    /* 构建页保护标志 */
    uint32_t page_prot = VMM_PROT_USER | VMM_PROT_NOCACHE;
    if (prot & 0x01) { /* PROT_READ */
        page_prot |= VMM_PROT_READ;
    }
    if (prot & 0x02) { /* PROT_WRITE */
        page_prot |= VMM_PROT_WRITE;
    }

    /* 映射物理页到用户空间 */
    paddr_t phys_base = region->phys_addr + start_page;
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t vaddr = user_base + i * PAGE_SIZE;
        paddr_t  paddr = phys_base + i * PAGE_SIZE;

        if (mm->map(proc->page_dir_phys, vaddr, paddr, page_prot) != 0) {
            /* 映射失败,尝试回滚 */
            pr_err("physmem: failed to map page %u at 0x%08x", i, vaddr);
            if (mm->unmap) {
                for (uint32_t j = 0; j < i; j++) {
                    mm->unmap(proc->page_dir_phys, user_base + j * PAGE_SIZE);
                }
            }
            return 0;
        }
    }

    /* 返回用户空间地址(带页内偏移) */
    uint32_t user_addr = user_base + (offset & (PAGE_SIZE - 1));

    pr_debug("physmem: mapped %u pages at user 0x%08x (phys 0x%08x)", num_pages, user_addr,
             (uint32_t)(region->phys_addr + offset));

    return user_addr;
}
