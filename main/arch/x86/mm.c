/**
 * @file mm.c
 * @brief x86 内存管理实现
 */

#include <arch/mmu.h>

#include <asm/multiboot.h>
#include <xnix/stdio.h>

/* 引用 Multiboot 信息指针 */
extern struct multiboot_info *multiboot_info_ptr;

extern char _kernel_end[];

void arch_get_memory_range(paddr_t *start, paddr_t *end) {
    struct multiboot_info *mb = multiboot_info_ptr;

    /* 内核结束地址 */
    *start = PAGE_ALIGN_UP((paddr_t)_kernel_end);

    /* 检查模块,避免覆盖模块内存 */
    if (mb->flags & MULTIBOOT_INFO_MODS) {
        if (mb->mods_count > 0) {
            struct multiboot_mod_list *mods         = (struct multiboot_mod_list *)mb->mods_addr;
            uint32_t                   mod_end_addr = 0;
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
    if (mb->flags & MULTIBOOT_INFO_MEMORY) {
        /* mem_upper 是从 1MB 开始的高端内存大小(KB) */
        *end = (1024 + mb->mem_upper) * 1024;
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
