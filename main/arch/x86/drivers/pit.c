/**
 * @file pit.c
 * @brief x86 8254 PIT 驱动
 * @author XiaoXiu
 */

#include <arch/cpu.h>

#include <drivers/timer.h>

#include <kernel/irq/irq.h>

#define PIT_CHANNEL0      0x40
#define PIT_CHANNEL2_DATA 0x42
#define PIT_CHANNEL2_GATE 0x61
#define PIT_CMD           0x43
#define PIT_FREQ          1193182

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

/**
 * 启动 PIT Channel 2 校准计时
 *
 * Channel 2 不产生中断,用于精确测量时间间隔.
 *
 * @param count 计数值,时间 = count / 1193182 秒
 */
void pit_calibration_start(uint16_t count) {
    /* 禁用 Speaker,启用 Channel 2 Gate */
    uint8_t gate = inb(PIT_CHANNEL2_GATE);
    gate &= ~0x02; /* 关闭 Speaker */
    gate |= 0x01;  /* 启用 Gate 2 */
    outb(PIT_CHANNEL2_GATE, gate);

    /* 配置 Channel 2: 模式 0 (单次计数), 先低后高 */
    outb(PIT_CMD, 0xB0); /* 10110000: Ch2, lobyte/hibyte, mode 0, binary */
    outb(PIT_CHANNEL2_DATA, count & 0xFF);
    outb(PIT_CHANNEL2_DATA, (count >> 8) & 0xFF);
}

/**
 * 检查 PIT Channel 2 校准是否完成
 *
 * @return 计数完成返回 true
 */
bool pit_calibration_done(void) {
    /* 读取 Gate 2 状态,OUT2 位在 bit 5 */
    uint8_t gate = inb(PIT_CHANNEL2_GATE);
    return (gate & 0x20) != 0;
}
