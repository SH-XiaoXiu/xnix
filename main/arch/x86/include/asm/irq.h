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
 * 未来扩展 APIC 时可以改为 256
 */
#define ARCH_NR_IRQS 16

#endif
