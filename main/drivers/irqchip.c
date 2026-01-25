/**
 * @file irqchip.c
 * @brief 中断控制器驱动框架
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <drivers/irqchip.h>

#define MAX_IRQS 16

static struct irqchip_driver *current_chip           = NULL;
static irq_handler_t          irq_handlers[MAX_IRQS] = {0};

int irqchip_register(struct irqchip_driver *drv) {
    if (!drv) {
        return -1;
    }
    current_chip = drv;
    return 0;
}

void irqchip_init(void) {
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
    if (irq < MAX_IRQS) {
        irq_handlers[irq] = handler;
    }
}

void irq_dispatch(uint8_t irq, struct irq_frame *frame) {
    if (irq < MAX_IRQS && irq_handlers[irq]) {
        irq_handlers[irq](frame);
    }
    /* IRQ 0 (timer) 的 EOI 由 sched_tick 负责,避免上下文切换时双重 EOI */
    if (irq != 0) {
        irq_eoi(irq);
    }
}
