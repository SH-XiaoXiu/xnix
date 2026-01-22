/**
 * @file isr.c
 * @brief 中断服务程序实现
 * @author XiaoXiu
 */

#include "isr.h"

#include "pic.h"

#include <xstd/stdio.h>

static irq_handler_t irq_handlers[16] = {0};

static const char *exception_names[] = {"Division By Zero",
                                        "Debug",
                                        "Non Maskable Interrupt",
                                        "Breakpoint",
                                        "Overflow",
                                        "Bound Range Exceeded",
                                        "Invalid Opcode",
                                        "Device Not Available",
                                        "Double Fault",
                                        "Coprocessor Segment Overrun",
                                        "Invalid TSS",
                                        "Segment Not Present",
                                        "Stack Fault",
                                        "General Protection Fault",
                                        "Page Fault",
                                        "Reserved",
                                        "x87 Floating Point",
                                        "Alignment Check",
                                        "Machine Check",
                                        "SIMD Floating Point",
                                        "Virtualization",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Reserved",
                                        "Security Exception",
                                        "Reserved"};

void isr_handler(struct interrupt_frame *frame) {
    kprintf("\n!!! EXCEPTION: %s (int=%d, err=0x%x)\n", exception_names[frame->int_no],
            frame->int_no, frame->err_code);
    kprintf("EIP=0x%x CS=0x%x EFLAGS=0x%x\n", frame->eip, frame->cs, frame->eflags);
    kprintf("EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x\n", frame->eax, frame->ebx, frame->ecx,
            frame->edx);

    /* 停机 */
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void irq_handler(struct interrupt_frame *frame) {
    uint8_t irq = frame->int_no - 32;

    if (irq_handlers[irq]) {
        irq_handlers[irq](frame);
    }

    pic_eoi(irq);
}

void irq_register(uint8_t irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}
