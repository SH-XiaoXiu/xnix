/**
 * @file mmu.h
 * @brief x86 MMU 常量定义
 */

#ifndef ASM_MMU_H
#define ASM_MMU_H

#include <xnix/config.h>
#include <xnix/types.h>

#define PAGE_SIZE  4096
#define PAGE_SHIFT 12
#define PAGE_MASK  (~(PAGE_SIZE - 1))

/* 高半核配置 */
#define KERNEL_VIRT_BASE       0xC0000000 /* 内核虚拟地址基址: 3GB */
#define KERNEL_DIRECT_MAP_SIZE ((uint32_t)CFG_KERNEL_DIRECT_MAP_MB * 1024 * 1024)

/* 物理地址 虚拟地址转换 */
#define PHYS_TO_VIRT(paddr) ((void *)((uintptr_t)(paddr) + KERNEL_VIRT_BASE))
#define VIRT_TO_PHYS(vaddr) ((paddr_t)((uintptr_t)(vaddr) - KERNEL_VIRT_BASE))

/* 判断虚拟地址是否在内核直接映射区 */
#define IS_KERNEL_DIRECT(vaddr)               \
    ((uint32_t)(vaddr) >= KERNEL_VIRT_BASE && \
     (uint32_t)(vaddr) < (KERNEL_VIRT_BASE + KERNEL_DIRECT_MAP_SIZE))

#endif
