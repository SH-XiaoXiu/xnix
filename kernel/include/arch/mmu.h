/**
 * @file mmu.h
 * @brief 内存管理单元 (MMU) 架构抽象接口
 *
 *   不同 CPU 架构的内存管理差异很大：
 *   - x86: 4KB 页，二级/三级页表，CR3 寄存器
 *   - ARM: 4KB/16KB/64KB 页，四级页表，TTBR 寄存器
 *   - RISC-V: Sv32/Sv39/Sv48 模式
 *
 *   通过抽象接口，上层内存管理代码（kmalloc、页分配器）可以保持平台无关,这是此内核的设计原则之一,平台无关喵。
 */

#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include <asm/mmu.h>
#include <xnix/types.h>

/* 地址运算宏 */
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & PAGE_MASK)   /* 向上取整到页边界，例如PAGE_ALIGN_UP(4097) = 8192 */
#define PAGE_ALIGN_DOWN(addr) ((addr) & PAGE_MASK)                      /* 向下取整到页边界，例如PAGE_ALIGN_DOWN(4097) = 4096 */
#define ADDR_TO_PFN(addr)     ((addr) >> PAGE_SHIFT)                   /* 地址转页帧号，例如ADDR_TO_PFN(0x12345000) = 0x12345 */
#define PFN_TO_ADDR(pfn)      ((pfn) << PAGE_SHIFT)                    /* 页帧号转地址，例如PFN_TO_ADDR(0x12345) = 0x12345000 */

/*
 * 物理地址类型
 *   32 位系统：物理地址和虚拟地址都是 32 位
 *   64 位系统：虚拟地址可能是 48 位，物理地址可能是 52 位
 *   PAE 模式：32 位 CPU 可以访问 64 位物理地址
 * 显式命名可以提醒编码过程区分物理/虚拟地址，避免混淆
 */
typedef uint32_t paddr_t;  /* 物理地址 */
typedef uint32_t vaddr_t;  /* 虚拟地址 */

/*
 * 架构相关函数（弱符号）
 *
 * 这些函数由 kernel/arch/xxx/mm.c 提供强符号实现
 */

/**
 * 架构 MMU 初始化
 * x86: 设置页表、启用分页（如果需要）
 */
void arch_mmu_init(void);

/**
 * 刷新 TLB (Translation Lookaside Buffer)
 *
 * TLB 是 CPU 缓存的页表项，修改页表后必须刷新
 * 否则 CPU 还会使用旧的映射
 * x86: invlpg 指令刷新单页，或重写 CR3 刷新全部
 */
void arch_tlb_flush_all(void);
void arch_tlb_flush_page(vaddr_t addr);

/**
 * 获取内核可用的物理内存范围
 *   BIOS/bootloader 会告诉内核哪些内存是可用的
 *   有些区域被 BIOS、ACPI 表、显存占用，不能使用
 *
 * @param start 输出参数，可用内存起始地址
 * @param end   输出参数，可用内存结束地址
 */
void arch_get_memory_range(paddr_t *start, paddr_t *end);

#endif
