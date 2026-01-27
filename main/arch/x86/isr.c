/**
 * @file isr.c
 * @brief x86 中断服务程序
 * @author XiaoXiu
 */

#include <asm/irq_defs.h>
#include <kernel/irq/irq.h>
#include <xnix/debug.h>
#include <xnix/stdio.h>
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
        klog(LOG_ERR, "User exception: %s at EIP=0x%x", exception_names[frame->int_no], frame->eip);
        process_terminate_current(frame->int_no);
        /* 不返回 */
    }

    /* 内核态异常 → panic(保留现有逻辑) */

    /* 打印通用寄存器 */
    klog(LOG_ERR, "--- Register Dump ---");
    klog(LOG_ERR, "EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x", frame->eax, frame->ebx,
         frame->ecx, frame->edx);
    klog(LOG_ERR, "ESI: 0x%08x  EDI: 0x%08x  EBP: 0x%08x  ESP: 0x%08x", frame->esi, frame->edi,
         frame->ebp, frame->esp);
    klog(LOG_ERR, "DS:  0x%04x      CS:  0x%04x      EFLAGS: 0x%08x", frame->ds, frame->cs,
         frame->eflags);

    panic("KERNEL EXCEPTION: %s (int=%d, err=0x%x) at EIP=0x%x", exception_names[frame->int_no],
          frame->int_no, frame->err_code, frame->eip);
}

/* IRQ 处理 */
void irq_handler(struct irq_frame *frame) {
    uint8_t irq = frame->int_no - 32;
    irq_dispatch(irq, frame);
}
