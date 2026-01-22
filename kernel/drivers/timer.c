/**
 * @file timer.c
 * @brief 定时器驱动框架
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <drivers/timer.h>

static struct timer_driver *current_timer = NULL;
static timer_callback_t     tick_callback = NULL;
static volatile uint64_t    tick_count    = 0;

int timer_register(struct timer_driver *drv) {
    if (!drv) {
        return -1;
    }
    /* 只支持一个定时器 */
    current_timer = drv;
    return 0;
}

void timer_init(uint32_t freq) {
    if (current_timer && current_timer->init) {
        current_timer->init(freq);
    }
}

uint64_t timer_get_ticks(void) {
    return tick_count;
}

void timer_set_callback(timer_callback_t cb) {
    tick_callback = cb;
}

void timer_tick(void) {
    tick_count++;
    if (tick_callback) {
        tick_callback();
    }
}
