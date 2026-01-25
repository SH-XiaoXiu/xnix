/**
 * @file mm.c
 * @brief x86 内存管理实现
 */

#include <arch/mmu.h>

#include <asm/multiboot.h>
#include <xnix/stdio.h>

extern char _kernel_end[];

void arch_get_memory_range(paddr_t *start, paddr_t *end) {
    struct multiboot_info *mb = (struct multiboot_info *)multiboot_info_ptr;

    *start = PAGE_ALIGN_UP((paddr_t)_kernel_end);

    /* 优先使用 mem_upper ,简单方式 */
    if (mb->flags & MB_INFO_MEMORY) {
        /* mem_upper 是从 1MB 开始的高端内存大小(KB) */
        *end = (1024 + mb->mem_upper) * 1024;
        return;
    }

    /* 没有内存信息,保守估计 4MB */
    kprintf("Warning: No memory info from bootloader, assuming 4MB\n");
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
