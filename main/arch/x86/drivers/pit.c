/**
 * @file pit.c
 * @brief x86 8254 PIT 驱动
 * @author XiaoXiu
 */

#include <arch/cpu.h>

#include <drivers/timer.h>

#include <kernel/irq/irq.h>

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182

static volatile uint64_t pit_ticks = 0;

/* IRQ0 处理函数 */
static void pit_irq_handler(irq_frame_t *frame) {
    (void)frame;
    pit_ticks++;
    timer_tick();
}

static void pit_init(uint32_t freq) {
    uint32_t divisor = PIT_FREQ / freq;

    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    /* 注册 IRQ0 并使能 */
    irq_set_handler(0, pit_irq_handler);
    irq_enable(0);
}

static uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

static struct timer_driver pit_timer = {
    .name      = "8254-pit",
    .init      = pit_init,
    .get_ticks = pit_get_ticks,
};

void pit_register(void) {
    timer_register(&pit_timer);
}
