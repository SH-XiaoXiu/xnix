/**
 * @file irq.h
 * @brief 中断请求 (IRQ)
 * @author XiaoXiu
 * @date 2026-01-22
 *
 * IRQ 子系统负责:
 * 管理中断处理函数表
 * 分发中断到具体 handler
 * 提供中断控制器硬件抽象层
 */

#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

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
 * @brief 中断控制器硬件抽象接口
 *
 * 具体的中断控制器驱动(PIC,APIC 等)需实现这些操作
 */
struct irqchip_ops {
    const char *name;
    void (*init)(void);
    void (*enable)(uint8_t irq);
    void (*disable)(uint8_t irq);
    void (*eoi)(uint8_t irq);
};

/**
 * @brief 设置中断控制器
 *
 * 由平台驱动(PIC,APIC 等)调用,注册硬件操作接口
 */
void irq_set_chip(const struct irqchip_ops *ops);

/**
 * @brief 初始化 IRQ 子系统
 *
 * 初始化中断控制器硬件
 */
void irq_init(void);

/**
 * @brief 使能指定 IRQ
 */
void irq_enable(uint8_t irq);

/**
 * @brief 禁用指定 IRQ
 */
void irq_disable(uint8_t irq);

/**
 * @brief 发送 EOI (End of Interrupt)
 */
void irq_eoi(uint8_t irq);

/**
 * @brief 注册 IRQ 处理函数
 *
 * @param irq IRQ 编号
 * @param handler 处理函数
 */
void irq_set_handler(uint8_t irq, irq_handler_t handler);

/**
 * @brief IRQ 分发
 *
 * 由架构层中断入口调用,分发到具体的处理函数
 *
 * @param irq IRQ 编号
 * @param frame 中断帧
 */
void irq_dispatch(uint8_t irq, struct irq_frame *frame);

#endif
