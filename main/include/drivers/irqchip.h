/**
 * @file irqchip.h
 * @brief 中断控制器驱动接口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#ifndef DRIVERS_IRQCHIP_H
#define DRIVERS_IRQCHIP_H

#include <xnix/types.h>

/**
 * @brief 中断帧(由架构层定义具体布局)
 */
struct irq_frame;

/**
 * @brief IRQ 处理函数类型
 */
typedef void (*irq_handler_t)(struct irq_frame *frame);

/**
 * @brief 中断控制器驱动操作接口
 */
struct irqchip_driver {
    const char *name;
    void (*init)(void);
    void (*enable)(uint8_t irq);
    void (*disable)(uint8_t irq);
    void (*eoi)(uint8_t irq);
};

/**
 * @brief 注册中断控制器驱动
 */
int irqchip_register(struct irqchip_driver *drv);

/**
 * @brief 初始化中断控制器
 */
void irqchip_init(void);

/**
 * @brief 使能指定 IRQ
 */
void irq_enable(uint8_t irq);

/**
 * @brief 禁用指定 IRQ
 */
void irq_disable(uint8_t irq);

/**
 * @brief 发送 EOI
 */
void irq_eoi(uint8_t irq);

/**
 * @brief 注册 IRQ 处理函数
 */
void irq_set_handler(uint8_t irq, irq_handler_t handler);

/**
 * @brief IRQ 分发(由架构层中断入口调用)
 */
void irq_dispatch(uint8_t irq, struct irq_frame *frame);

#endif
