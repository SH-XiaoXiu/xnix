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
struct irq_regs;
typedef struct irq_regs irq_frame_t;

/**
 * @brief IRQ 处理函数类型
 */
typedef void (*irq_handler_t)(irq_frame_t *frame);

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
void irq_dispatch(uint8_t irq, irq_frame_t *frame);

/*
 * IRQ 用户态绑定
 *
 * 允许用户态进程接收 IRQ 通知.
 * 绑定后,IRQ 触发时会向指定的 notification 发送信号.
 */

struct ipc_notification; /* 前向声明 */

/**
 * @brief 绑定 IRQ 到 notification
 *
 * @param irq   IRQ 编号
 * @param notif notification 对象指针
 * @param bits  触发时发送的信号位
 * @return 0 成功,负数失败
 */
int irq_bind_notification(uint8_t irq, struct ipc_notification *notif, uint32_t bits);

/**
 * @brief 解除 IRQ 绑定
 *
 * @param irq IRQ 编号
 * @return 0 成功,负数失败
 */
int irq_unbind_notification(uint8_t irq);

/**
 * @brief 向 IRQ 缓冲区写入数据
 *
 * 由 IRQ 处理器调用,将数据存入缓冲区供用户态读取.
 *
 * @param irq  IRQ 编号
 * @param data 数据字节
 */
void irq_user_push(uint8_t irq, uint8_t data);

/**
 * @brief 从 IRQ 缓冲区读取数据
 *
 * @param irq   IRQ 编号
 * @param buf   目标缓冲区
 * @param size  缓冲区大小
 * @param block 是否阻塞等待
 * @return 读取的字节数,负数表示错误
 */
int irq_user_read(uint8_t irq, uint8_t *buf, size_t size, bool block);

#endif
