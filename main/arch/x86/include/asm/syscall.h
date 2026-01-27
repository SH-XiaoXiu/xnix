/**
 * @file asm/syscall.h
 * @brief x86 系统调用参数提取
 *
 * x86 寄存器约定:
 * - eax: 系统调用号
 * - ebx, ecx, edx, esi, edi, ebp: 参数 0-5
 * - 返回值通过 eax 返回
 */

#ifndef ASM_X86_SYSCALL_H
#define ASM_X86_SYSCALL_H

#include <arch/syscall.h>
#include <asm/irq_defs.h>

/**
 * 从中断帧提取系统调用参数
 */
static inline void x86_extract_syscall_args(const struct irq_regs *regs,
                                            struct syscall_args *args) {
    args->nr     = regs->eax;
    args->arg[0] = regs->ebx;
    args->arg[1] = regs->ecx;
    args->arg[2] = regs->edx;
    args->arg[3] = regs->esi;
    args->arg[4] = regs->edi;
    args->arg[5] = regs->ebp;
}

/**
 * 将系统调用结果写回中断帧
 */
static inline void x86_set_syscall_result(struct irq_regs *regs,
                                          const struct syscall_result *result) {
    regs->eax = (uint32_t)result->retval;
}

#endif /* ASM_X86_SYSCALL_H */
