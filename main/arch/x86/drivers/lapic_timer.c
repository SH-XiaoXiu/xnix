/**
 * @file lapic_timer.c
 * @brief LAPIC Timer 驱动封装
 *
 * 将 LAPIC Timer 封装为 timer 框架驱动,提供统一接口.
 */

#include <arch/smp.h>

#include <drivers/timer.h>

#include <asm/apic.h>
#include <kernel/irq/irq.h>
#include <kernel/sched/sched.h>

/**
 * LAPIC Timer 中断处理函数
 *
 * 每个 CPU 都有自己的 LAPIC Timer,但只有 BSP 负责维护全局 tick 计数.
 * 所有 CPU 都需要调用 sched_tick() 来处理自己核心上的调度.
 */
static void lapic_timer_irq_handler(irq_frame_t *frame) {
    (void)frame;

    if (cpu_current_id() == 0) {
        /* BSP: 更新全局 tick 计数并触发调度 */
        timer_tick();
    } else {
        /* AP: 只触发本核心的调度 */
        sched_tick();
    }
}

/**
 * LAPIC Timer 驱动初始化
 *
 * @param freq 目标频率 (Hz)
 */
static void lapic_timer_drv_init(uint32_t freq) {
    /* 注册 IRQ0 处理函数 (LAPIC Timer 使用向量 0x20 = IRQ0) */
    irq_set_handler(0, lapic_timer_irq_handler);

    /* 初始化 LAPIC Timer */
    lapic_timer_init(freq);
}

static struct timer_driver lapic_timer_driver = {
    .name      = "lapic-timer",
    .init      = lapic_timer_drv_init,
    .get_ticks = timer_get_ticks, /* 使用框架的 tick_count */
};

/**
 * 注册 LAPIC Timer 驱动到 timer 框架
 * 调用此函数会覆盖之前注册的 PIT 驱动.
 */
void lapic_timer_register(void) {
    timer_register(&lapic_timer_driver);
}
