#ifndef ARCH_X86_IRQ_DEFS_H
#define ARCH_X86_IRQ_DEFS_H

#include <xnix/types.h>

/* x86 中断帧定义 (与 isr.s 压栈顺序一致) */
struct irq_regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

#endif
