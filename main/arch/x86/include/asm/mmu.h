/**
 * @file mmu.h
 * @brief x86 MMU 常量定义
 */

#ifndef ASM_MMU_H
#define ASM_MMU_H

#define PAGE_SIZE  4096
#define PAGE_SHIFT 12
#define PAGE_MASK  (~(PAGE_SIZE - 1))

#endif
