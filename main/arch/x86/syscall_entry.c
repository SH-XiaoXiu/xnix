/**
 * @file arch/x86/syscall_entry.c
 * @brief x86 系统调用入口
 *
 * 从中断帧提取参数,调用平台无关的 syscall_dispatch,回写结果.
 */

#include <arch/syscall.h>

#include <asm/irq_defs.h>
#include <asm/syscall.h>

/**
 * x86 系统调用处理函数(由 isr.s 调用)
 */
void syscall_handler(struct irq_regs *regs) {
    struct syscall_args args;
    x86_extract_syscall_args(regs, &args);

    struct syscall_result result = syscall_dispatch(&args);

    x86_set_syscall_result(regs, &result);
}
