/**
 * @file isr.c
 * @brief x86 中断服务程序
 * @author XiaoXiu
 */

#include <asm/apic.h>
#include <asm/irq.h>
#include <asm/irq_defs.h>
#include <xnix/debug.h>
#include <xnix/irq.h>
#include <xnix/stdio.h>
#include <xnix/thread_def.h>
#include <xnix/types.h>
#include <xnix/vmm.h>

/* x86 中断帧定义 (使用 asm/irq_defs.h 中的定义) */
#define irq_frame irq_regs

static const char *exception_names[] = {
    "Division By Zero", "Debug",          "NMI",         "Breakpoint",    "Overflow",
    "Bound Range",      "Invalid Opcode", "Device N/A",  "Double Fault",  "Coprocessor",
    "Invalid TSS",      "Segment N/P",    "Stack Fault", "GPF",           "Page Fault",
    "Reserved",         "x87 FP",         "Alignment",   "Machine Check", "SIMD FP",
    "Virtualization",   "Reserved",       "Reserved",    "Reserved",      "Reserved",
    "Reserved",         "Reserved",       "Reserved",    "Reserved",      "Reserved",
    "Security",         "Reserved"};

/* 声明进程终止函数 */
void process_terminate_current(int signal);

/* CPU 异常处理 */
void isr_handler(struct irq_frame *frame) {
    /* 页错误特殊处理 */
    if (frame->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        vmm_page_fault(frame, cr2);
        return;
    }

    /* 判断异常来源: CS 低 2 位是 RPL */
    bool from_user = (frame->cs & 0x03) == 3;

    if (from_user) {
        /* 用户态异常 → 终止进程 */
        pr_err("User exception: %s at EIP=0x%x", exception_names[frame->int_no], frame->eip);
        process_terminate_current(frame->int_no);
        /* 不返回 */
    }

    /* 内核态异常 → panic(保留现有逻辑) */

    /* 打印通用寄存器 */
    pr_err("--- Register Dump ---");
    pr_err("EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x", frame->eax, frame->ebx, frame->ecx,
           frame->edx);
    pr_err("ESI: 0x%08x  EDI: 0x%08x  EBP: 0x%08x  ESP: 0x%08x", frame->esi, frame->edi, frame->ebp,
           frame->esp);
    pr_err("DS:  0x%04x      CS:  0x%04x      EFLAGS: 0x%08x", frame->ds, frame->cs, frame->eflags);

    panic("KERNEL EXCEPTION: %s (int=%d, err=0x%x) at EIP=0x%x", exception_names[frame->int_no],
          frame->int_no, frame->err_code, frame->eip);
}

/* IRQ 处理 */
void irq_handler(struct irq_frame *frame) {
    uint8_t irq = frame->int_no - 32;
    irq_dispatch(irq, frame);
}

/*
 * IPI 处理函数
 *
 * 处理核间中断:
 * - RESCHED: 触发重新调度
 * - TLB: TLB shootdown (待实现)
 * - PANIC: 停止当前核
 */
void ipi_handler(struct irq_frame *frame) {
    uint8_t vector       = (uint8_t)frame->int_no;
    bool    need_resched = false;

    switch (vector) {
    case IPI_VECTOR_RESCHED:
        /* 调度 IPI:标记需要重新调度 */
        need_resched = true;
        break;

    case IPI_VECTOR_TLB:
        /* TLB shootdown: 待实现 */
        break;

    case IPI_VECTOR_PANIC:
        /* Panic IPI: 停止当前核 */
        kprintf("CPU halted by panic IPI\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
        break;

    default:
        pr_warn("Unknown IPI vector 0x%02x", vector);
        break;
    }

    /* 发送 EOI(必须在 schedule 之前) */
    lapic_eoi();

    /* 如果需要,执行重新调度 */
    if (need_resched) {
        schedule();
    }
}
