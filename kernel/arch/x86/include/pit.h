/**
 * @file pit.h
 * @brief 8254 PIT 可编程间隔定时器
 * @author XiaoXiu
 */

#ifndef ARCH_PIT_H
#define ARCH_PIT_H

#include <arch/types.h>

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_CMD      0x43

#define PIT_FREQ 1193182 /* PIT 基础频率 Hz */

/**
 * @brief 初始化 PIT
 * @param freq 期望的中断频率 (Hz)
 */
void pit_init(uint32_t freq);

/**
 * @brief 获取 tick 计数
 */
uint32_t pit_get_ticks(void);

#endif
