#include <asm-generic/errno.h>
#include <asm/irq_defs.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>

/*
 * 系统调用处理函数
 * 参数来自寄存器:
 * eax: 系统调用号
 * ebx: 参数 1
 * ecx: 参数 2
 * edx: 参数 3
 * esi: 参数 4
 * edi: 参数 5
 *
 * 返回值通过 eax 返回
 */
/* 声明 thread_exit */
extern void thread_exit(int code);

void syscall_handler(struct irq_regs *regs) {
    uint32_t syscall_num = regs->eax;
    int      ret         = -ENOSYS;
    switch (syscall_num) {
    case SYS_EXIT:
        /* sys_exit(int code) */
        thread_exit((int)regs->ebx);
        break;

    case SYS_PUTC:
        /* sys_putc(char c) */
        kputc((char)(regs->ebx & 0xFF));
        ret = 0;
        break;

    default:
        pr_warn("Unknown syscall: %d", syscall_num);
        ret = -1;
        break;
    }

    regs->eax = ret;
}
