/**
 * @file lapic_timer.c
 * @brief LAPIC Timer 驱动封装
 *
 * 将 LAPIC Timer 封装为 timer 框架驱动,提供统一接口.
 * 使用驱动注册框架,支持 Boot 裁切
 */

#include <arch/smp.h>

#include <drivers/timer.h>

#include <asm/apic.h>
#include <asm/smp_defs.h>
#include <kernel/irq/irq.h>
#include <kernel/sched/sched.h>
#include <xnix/driver.h>

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

/**
 * LAPIC Timer 探测:检查 APIC 是否可用
 */
static bool lapic_timer_probe(void) {
    extern struct smp_info g_smp_info;
    return g_smp_info.apic_available;
}

/* 旧接口(保留兼容) */
static struct timer_driver lapic_timer_driver = {
    .name      = "lapic-timer",
    .init      = lapic_timer_drv_init,
    .get_ticks = timer_get_ticks, /* 使用框架的 tick_count */
};

/* 新驱动注册框架 */
static struct timer_driver_ext lapic_timer_driver_ext = {
    .name      = "lapic",
    .priority  = 100, /* 高优先级,优先于 PIT */
    .probe     = lapic_timer_probe,
    .init      = lapic_timer_drv_init,
    .get_ticks = timer_get_ticks,
    .next      = NULL,
};

/**
 * 注册 LAPIC Timer 驱动到 timer 框架
 * 调用此函数会覆盖之前注册的 PIT 驱动.
 */
void lapic_timer_register(void) {
    /* 注册到新框架 */
    timer_drv_register(&lapic_timer_driver_ext);
    /* 同时注册到旧框架(兼容) */
    timer_register(&lapic_timer_driver);
}
