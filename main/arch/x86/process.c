#include <arch/cpu.h>
#include <arch/x86/gdt.h>
#include <arch/x86/tss.h>

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h> /* pr_info */

void arch_thread_switch(struct thread *next) {
    /* 切换地址空间 */
    if (next->owner && next->owner->page_dir_phys) {
        const struct mm_operations *mm = mm_get_ops();
        if (mm && mm->switch_as) {
            mm->switch_as(next->owner->page_dir_phys);
        }
    }

    /* 更新 TSS 的内核栈指针 (ESP0) */
    if (next->stack) {
        uint32_t esp0 = (uint32_t)next->stack + next->stack_size;
        tss_set_stack(KERNEL_DS, esp0);
    }
}
