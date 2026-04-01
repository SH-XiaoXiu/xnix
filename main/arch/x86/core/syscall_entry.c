/**
 * @file arch/x86/syscall_entry.c
 * @brief x86 系统调用入口
 *
 * 从中断帧提取参数,调用平台无关的 syscall_dispatch,回写结果.
 */

#include <arch/cpu.h>
#include <arch/syscall.h>

#include <asm/irq_defs.h>
#include <asm/syscall.h>
#include <xnix/process_def.h>
#include <xnix/thread_def.h>

/**
 * x86 系统调用处理函数(由 isr.s 调用)
 */
void syscall_handler(struct irq_regs *regs) {
    struct syscall_args args;
    x86_extract_syscall_args(regs, &args);

    struct syscall_result result = syscall_dispatch(&args);

    /* 检查线程是否被强制退出(thread_force_exit 设置) */
    struct thread *current = sched_current();
    if (current && current->state == THREAD_EXITED) {
        /*
         * 线程被强制退出.需要加入僵尸链表然后让出 CPU.
         * thread_force_exit 已经设置了 is_detached = true,
         * 所以 sched_cleanup_zombie 会释放这个线程.
         */
        cpu_irq_disable();
        thread_add_to_zombie_list(current);
        schedule();
        /* 不应该返回 */
        while (1) {
            cpu_halt();
        }
    }

    /* 返回用户态前检查信号 */
    process_check_signals();

    x86_set_syscall_result(regs, &result);
}
