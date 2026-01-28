#include <arch/cpu.h>
#include <arch/x86/gdt.h>
#include <arch/x86/tss.h>

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h> /* pr_info */
#include <xnix/vmm.h>   /* vmm_get_kernel_pd, vmm_switch_pd */

void arch_thread_switch(struct thread *next) {
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->switch_as) {
        return;
    }

    /* 切换地址空间 */
    if (next->owner && next->owner->page_dir_phys) {
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
