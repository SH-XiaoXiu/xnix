#include <arch/cpu.h>

#include <asm/gdt.h>
#include <asm/tss.h>
#include <xnix/mm_ops.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h> /* pr_info */
#include <xnix/thread_def.h>
#include <xnix/vmm.h> /* vmm_get_kernel_pd, vmm_switch_pd */

void arch_thread_switch(struct thread *next) {
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->switch_as) {
        return;
    }

    /* 切换地址空间 */
    /*
     * 不能访问 EXITED 状态线程的 owner,因为进程可能已被释放.
     * 这发生在多核场景:process_exit 唤醒父进程后,父进程在另一个 CPU
     * 上调用 waitpid/process_unref 释放进程,但当前 CPU 的线程还在运行.
     */
    if (next->state == THREAD_EXITED) {
        /* 退出中的线程使用内核页目录 */
        void *kernel_pd = vmm_get_kernel_pd();
        if (kernel_pd) {
            mm->switch_as(kernel_pd);
        }
    } else if (next->owner && next->owner->page_dir_phys) {
        /* 用户进程线程:切换到进程的页目录 */
        mm->switch_as(next->owner->page_dir_phys);
    } else {
        /* 内核线程/idle线程:切换到内核页目录 */
        void *kernel_pd = vmm_get_kernel_pd();
        if (kernel_pd) {
            mm->switch_as(kernel_pd);
        }
    }

    /* 更新 TSS 的内核栈指针 (ESP0) */
    if (next->stack) {
        uint32_t esp0 = (uint32_t)next->stack + next->stack_size;
        tss_set_stack(KERNEL_DS, esp0);
    }
}
