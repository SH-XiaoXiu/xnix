/**
 * @file isr.c
 * @brief x86 中断服务程序
 * @author XiaoXiu
 */

#include <arch/cpu.h>

#include <xstd/stdint.h>
#include <xstd/stdio.h>

#include <drivers/irqchip.h>

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
    kprintf("\n%R!!! EXCEPTION%N: %s (int=%d, err=0x%x)\n", exception_names[frame->int_no],
            frame->int_no, frame->err_code);
    kprintf("EIP=0x%x CS=0x%x EFLAGS=0x%x\n", frame->eip, frame->cs, frame->eflags);

    while (1) {
        cpu_stop();
    }
}

/* IRQ 处理 */
void irq_handler(struct irq_frame *frame) {
    uint8_t irq = frame->int_no - 32;
    irq_dispatch(irq, frame);
}
