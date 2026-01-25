/**
 * @file isr.c
 * @brief x86 中断服务程序
 * @author XiaoXiu
 */

#include <kernel/irq/irq.h>
#include <xnix/debug.h>
#include <xnix/stdio.h>
#include <xnix/types.h>

/* x86 中断帧定义 */
struct irq_frame {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

static const char *exception_names[] = {
    "Division By Zero", "Debug",          "NMI",         "Breakpoint",    "Overflow",
    "Bound Range",      "Invalid Opcode", "Device N/A",  "Double Fault",  "Coprocessor",
    "Invalid TSS",      "Segment N/P",    "Stack Fault", "GPF",           "Page Fault",
    "Reserved",         "x87 FP",         "Alignment",   "Machine Check", "SIMD FP",
    "Virtualization",   "Reserved",       "Reserved",    "Reserved",      "Reserved",
    "Reserved",         "Reserved",       "Reserved",    "Reserved",      "Reserved",
    "Security",         "Reserved"};

/* CPU 异常处理 */
void isr_handler(struct irq_frame *frame) {
    /* 打印通用寄存器 */
    klog(LOG_ERR, "--- Register Dump ---");
    klog(LOG_ERR, "EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x", frame->eax, frame->ebx,
         frame->ecx, frame->edx);
    klog(LOG_ERR, "ESI: 0x%08x  EDI: 0x%08x  EBP: 0x%08x  ESP: 0x%08x", frame->esi, frame->edi,
         frame->ebp, frame->esp);
    klog(LOG_ERR, "DS:  0x%04x      CS:  0x%04x      EFLAGS: 0x%08x", frame->ds, frame->cs,
         frame->eflags);

    /* 如果是页错误 (Page Fault, #14),打印 CR2 */
    if (frame->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        klog(LOG_ERR, "CR2: 0x%08x (Page Fault Address)", cr2);
    }

    panic("EXCEPTION: %s (int=%d, err=0x%x) at EIP=0x%x", exception_names[frame->int_no],
          frame->int_no, frame->err_code, frame->eip);
}

/* IRQ 处理 */
void irq_handler(struct irq_frame *frame) {
    uint8_t irq = frame->int_no - 32;
    irq_dispatch(irq, frame);
}
