/**
 * @file mm.c
 * @brief x86 内存管理实现
 */

#include <arch/mmu.h>

#include <asm/mmu.h>
#include <asm/multiboot.h>
#include <xnix/stdio.h>

/* 引用 Multiboot 信息指针 */
extern struct multiboot_info *multiboot_info_ptr;

extern char _kernel_end[];

static paddr_t clamp_u64_to_paddr(uint64_t v) {
    if (v > 0xFFFFFFFFULL) {
        return 0xFFFFFFFFu;
    }
    return (paddr_t)v;
}

uint32_t arch_get_memory_map(struct arch_mem_region *regions, uint32_t max_regions) {
    struct multiboot_info *mb = multiboot_info_ptr;
    if (!mb || !regions || !max_regions) {
        return 0;
    }

    if (!(mb->flags & MULTIBOOT_INFO_MEM_MAP) || !mb->mmap_length || !mb->mmap_addr) {
        return 0;
    }

    uint32_t out = 0;
    uint32_t off = 0;
    while (off < mb->mmap_length && out < max_regions) {
        struct multiboot_mmap_entry *e =
            (struct multiboot_mmap_entry *)PHYS_TO_VIRT(mb->mmap_addr + off);

        uint64_t start64 = e->addr;
        uint64_t end64   = e->addr + e->len;

        if (e->type == MULTIBOOT_MEMORY_AVAILABLE && end64 > start64) {
            if (start64 < 0x1000ULL) {
                start64 = 0x1000ULL;
            }
            if (start64 <= 0xFFFFFFFFULL) {
                if (end64 > 0x100000000ULL) {
                    end64 = 0x100000000ULL;
                }
                if (end64 > start64) {
                    regions[out].start = clamp_u64_to_paddr(start64);
                    regions[out].end   = clamp_u64_to_paddr(end64);
                    regions[out].type  = ARCH_MEM_USABLE;
                    out++;
                }
            }
        }

        off += e->size + sizeof(e->size);
    }

    return out;
}

void arch_get_memory_range(paddr_t *start, paddr_t *end) {
    struct multiboot_info *mb = multiboot_info_ptr;

    /* 内核结束地址(转换虚拟地址为物理地址) */
    *start = PAGE_ALIGN_UP(VIRT_TO_PHYS((paddr_t)_kernel_end));

    /* 检查模块,避免覆盖模块内存 */
    if (mb->flags & MULTIBOOT_INFO_MODS) {
        if (mb->mods_count > 0) {
            struct multiboot_mod_list *mods =
                (struct multiboot_mod_list *)PHYS_TO_VIRT(mb->mods_addr);
            uint32_t mod_end_addr = 0;
            for (uint32_t i = 0; i < mb->mods_count; i++) {
                if (mods[i].mod_end > mod_end_addr) {
                    mod_end_addr = mods[i].mod_end;
                }
            }
            /* 如果模块在内核之后,调整起始可用内存 */
            if (mod_end_addr > *start) {
                *start = PAGE_ALIGN_UP(mod_end_addr);
            }
        }
    }

    /* 优先使用 mem_upper ,简单方式 */
    if (mb->flags & MULTIBOOT_INFO_MEM_MAP) {
        struct arch_mem_region regions[32];
        uint32_t               count   = arch_get_memory_map(regions, 32);
        paddr_t                max_end = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (regions[i].end > max_end) {
                max_end = regions[i].end;
            }
        }
        if (max_end) {
            *end = max_end;
            if (*end > 0xC0000000u) {
                pr_warn("RAM above 3GB is ignored on non-PAE x86");
                *end = 0xC0000000u;
            }
            return;
        }
    }

    if (mb->flags & MULTIBOOT_INFO_MEMORY) {
        *end = (1024 + mb->mem_upper) * 1024;
        if (*end > 0xC0000000u) {
            pr_warn("RAM above 3GB is ignored on non-PAE x86");
            *end = 0xC0000000u;
        }
        return;
    }

    /* 没有内存信息,保守估计 4MB */
    pr_warn("No memory info from bootloader, assuming 4MB");
    *end = 4 * 1024 * 1024;
}

void arch_mmu_init(void) {
    /* x86 启动时已经在保护模式,暂不需要额外初始化 */
}

void arch_tlb_flush_all(void) {
    __asm__ volatile("mov %%cr3, %%eax\n"
                     "mov %%eax, %%cr3\n" ::
                         : "eax");
}

void arch_tlb_flush_page(vaddr_t addr) {
    __asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}
