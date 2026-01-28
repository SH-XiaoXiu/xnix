/**
 * @file smp_asm.h
 * @brief SMP 汇编和 C 共享的常量定义
 *
 * 此文件可被 .S 汇编文件和 .c 文件同时包含
 */

#ifndef ASM_SMP_ASM_H
#define ASM_SMP_ASM_H

/* Trampoline 加载地址 (必须在 1MB 以下,且 4KB 对齐) */
#define AP_TRAMPOLINE_ADDR 0x8000
#define AP_TRAMPOLINE_SEG  (AP_TRAMPOLINE_ADDR >> 4) /* 实模式段地址 */

/* LAPIC 地址 (使用 apic.h 中的定义更好,但汇编需要简单常量) */
#define LAPIC_BASE_ADDR 0xFEE00000
#define LAPIC_ID_OFFSET 0x020

/* MSR 定义 */
#define MSR_APIC_BASE   0x1B
#define MSR_APIC_X2APIC 0x400
#define MSR_APIC_ENABLE 0x800

/* CR0 位 */
#define CR0_PE 0x00000001 /* 保护模式使能 */
#define CR0_PG 0x80000000 /* 分页使能 */

#endif /* ASM_SMP_ASM_H */
