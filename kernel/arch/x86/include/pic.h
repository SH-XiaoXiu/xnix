/**
 * @file pic.h
 * @brief 8259 PIC 可编程中断控制器
 * @author XiaoXiu
 */

// x86早期平台上的硬件定时器，本质作用是按固定频率向 CPU 产生周期性中断

#ifndef ARCH_PIC_H
#define ARCH_PIC_H

#include <arch/types.h>

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

#define PIC_EOI     0x20

/**
 * @brief 初始化 8259 PIC，重映射 IRQ 到 0x20-0x2F
 */
void pic_init(void);

/**
 * @brief 发送 EOI 信号
 * @param irq IRQ 号 (0-15)
 */
void pic_eoi(uint8_t irq);

/**
 * @brief 屏蔽指定 IRQ
 */
void pic_mask(uint8_t irq);

/**
 * @brief 取消屏蔽指定 IRQ
 */
void pic_unmask(uint8_t irq);

#endif
