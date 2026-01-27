/**
 * @file irq.h
 * @brief x86 中断相关定义
 * @author XiaoXiu
 * @date 2026-01-25
 */

#ifndef ASM_X86_IRQ_H
#define ASM_X86_IRQ_H

/**
 * x86 中断向量数量
 *
 * 8259 PIC: 支持 16 个 IRQ (IRQ0-IRQ15)
 * APIC: 支持 24 个 I/O APIC 输入 + IPI 向量
 *
 * 这里定义的是外部 IRQ 数量, 不包括 IPI
 */
#define ARCH_NR_IRQS 24

/* IRQ 向量偏移 (与 PIC 兼容) */
#define IRQ_VECTOR_BASE 0x20

/* IPI 向量定义 */
#define IPI_VECTOR_RESCHED 0xF0
#define IPI_VECTOR_TLB     0xF1
#define IPI_VECTOR_PANIC   0xF2

#endif
