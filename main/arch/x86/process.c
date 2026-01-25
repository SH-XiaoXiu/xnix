#include <arch/cpu.h>
#include <arch/x86/gdt.h>
#include <arch/x86/tss.h>

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/stdio.h> /* pr_info */
#include <xnix/vmm.h>

void arch_thread_switch(struct thread *next) {
    /* 切换地址空间 */
    if (next->owner && next->owner->page_dir_phys) {
        vmm_switch_pd(next->owner->page_dir_phys);
    }

    /* 更新 TSS 的内核栈指针 (ESP0) */
    if (next->stack) {
        uint32_t esp0 = (uint32_t)next->stack + next->stack_size;
        tss_set_stack(KERNEL_DS, esp0);
    }
}
