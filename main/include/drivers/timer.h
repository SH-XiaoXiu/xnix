/**
 * @file timer.h
 * @brief 定时器驱动接口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include <xnix/types.h>

/**
 * @brief 定时器驱动操作接口
 */
struct timer_driver {
    const char *name;
    void (*init)(uint32_t freq);
    uint64_t (*get_ticks)(void);
};

/**
 * @brief 定时器回调函数类型
 */
typedef void (*timer_callback_t)(void);

/**
 * @brief 注册定时器驱动
 * @param drv 驱动结构指针
 * @return 0 成功，-1 失败
 */
int timer_register(struct timer_driver *drv);

/**
 * @brief 初始化定时器
 * @param freq 频率 (Hz)
 */
void timer_init(uint32_t freq);

/**
 * @brief 获取 tick 计数
 */
uint64_t timer_get_ticks(void);

/**
 * @brief 设置定时器回调（每次 tick 调用）
 */
void timer_set_callback(timer_callback_t cb);

/**
 * @brief 定时器中断处理（由中断处理程序调用）
 */
void timer_tick(void);

#endif
