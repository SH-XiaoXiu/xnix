/**
 * @file pit.c
 * @brief 8254 PIT 定时器实现
 * @author XiaoXiu
 */

#include "pit.h"

#include "isr.h"
#include "pic.h"

#include <arch/io.h>

#include <xstd/stdio.h>

static volatile uint32_t ticks = 0;

/* 前向声明调度器的 tick 处理 */
extern void sched_tick(void);

static void pit_handler(struct interrupt_frame *frame) {
    (void)frame;
    ticks++;
    /* 先发 EOI，因为 sched_tick 可能不返回 */
    pic_eoi(0);
    sched_tick();
}

void pit_init(uint32_t freq) {
    uint32_t divisor = PIT_FREQ / freq;

    /* 设置模式：通道0，先低后高，方波模式 */
    arch_outb(PIT_CMD, 0x36);

    /* 设置分频值 */
    arch_outb(PIT_CHANNEL0, divisor & 0xFF);
    arch_outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    /* 注册中断处理函数 */
    irq_register(0, pit_handler);

    /* 取消屏蔽 IRQ0 */
    pic_unmask(0);

    kprintf("PIT: initialized at %d Hz\n", freq);
}

uint32_t pit_get_ticks(void) {
    return ticks;
}
