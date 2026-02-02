/**
 * @file irq.c
 * @brief 中断请求 (IRQ) 子系统实现
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <asm/irq.h>
#include <kernel/irq/irq.h>

static const struct irqchip_ops *current_chip               = NULL;
static irq_handler_t             irq_handlers[ARCH_NR_IRQS] = {0};

void irq_set_chip(const struct irqchip_ops *ops) {
    current_chip = ops;
}

void irq_init(void) {
    if (current_chip && current_chip->init) {
        current_chip->init();
    }
}

void irq_enable(uint8_t irq) {
    if (current_chip && current_chip->enable) {
        current_chip->enable(irq);
    }
}

void irq_disable(uint8_t irq) {
    if (current_chip && current_chip->disable) {
        current_chip->disable(irq);
    }
}

void irq_eoi(uint8_t irq) {
    if (current_chip && current_chip->eoi) {
        current_chip->eoi(irq);
    }
}

void irq_set_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < ARCH_NR_IRQS) {
        irq_handlers[irq] = handler;
    }
}

void irq_dispatch(uint8_t irq, irq_frame_t *frame) {
    if (irq < ARCH_NR_IRQS && irq_handlers[irq]) {
        irq_handlers[irq](frame);
        /* IRQ 0 (timer) 的 EOI 由 handler (sched_tick) 负责 */
        if (irq != 0) {
            irq_eoi(irq);
        }
    } else {
        /* 无 handler 时直接发送 EOI,避免中断被阻塞 */
        irq_eoi(irq);
    }
}
