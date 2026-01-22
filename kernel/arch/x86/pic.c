/**
 * @file pic.c
 * @brief 8259 PIC 初始化
 * @author XiaoXiu
 */

#include "pic.h"
#include <arch/io.h>

void pic_init(void) {
    /* ICW1: 开始初始化序列 */
    arch_outb(PIC1_CMD, 0x11);
    arch_outb(PIC2_CMD, 0x11);

    /* ICW2: 设置中断向量偏移 */
    arch_outb(PIC1_DATA, 0x20);  /* IRQ 0-7  -> 中断 32-39 */
    arch_outb(PIC2_DATA, 0x28);  /* IRQ 8-15 -> 中断 40-47 */

    /* ICW3: 设置级联 */
    arch_outb(PIC1_DATA, 0x04);  /* IRQ2 连接从片 */
    arch_outb(PIC2_DATA, 0x02);  /* 从片连接到 IRQ2 */

    /* ICW4: 8086 模式 */
    arch_outb(PIC1_DATA, 0x01);
    arch_outb(PIC2_DATA, 0x01);

    /* 屏蔽所有中断 */
    arch_outb(PIC1_DATA, 0xFF);
    arch_outb(PIC2_DATA, 0xFF);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        arch_outb(PIC2_CMD, PIC_EOI);
    }
    arch_outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = arch_inb(port) | (1 << irq);
    arch_outb(port, mask);
}

void pic_unmask(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = arch_inb(port) & ~(1 << irq);
    arch_outb(port, mask);
}
