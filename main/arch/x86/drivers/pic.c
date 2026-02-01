/**
 * @file pic.c
 * @brief x86 8259 PIC 驱动
 * @author XiaoXiu
 *
 * 使用驱动注册框架,支持 Boot 裁切
 */

#include <arch/cpu.h>

#include <kernel/irq/irq.h>
#include <xnix/driver.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_init(void) {
    /* ICW1 */
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    /* ICW2: 重映射到 0x20-0x2F */
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    /* ICW3: 级联 */
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    /* ICW4: 8086 模式 */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    /* 屏蔽所有中断 */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

static void pic_enable(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    outb(port, inb(port) & ~(1 << irq));
}

static void pic_disable(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) {
        irq -= 8;
    }
    outb(port, inb(port) | (1 << irq));
}

static void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

/**
 * PIC 探测:总是可用(作为 fallback)
 */
static bool pic_probe(void) {
    return true;
}

/* 使用新的驱动注册框架 */
static struct irqchip_driver pic_driver = {
    .name     = "pic",
    .priority = 10, /* 低优先级,作为 fallback */
    .probe    = pic_probe,
    .init     = pic_init,
    .enable   = pic_enable,
    .disable  = pic_disable,
    .eoi      = pic_eoi,
    .next     = NULL,
};

/* 保留旧接口以兼容现有代码 */
static const struct irqchip_ops pic_chip = {
    .name    = "8259-pic",
    .init    = pic_init,
    .enable  = pic_enable,
    .disable = pic_disable,
    .eoi     = pic_eoi,
};

void pic_register(void) {
    /* 注册到新框架 */
    irqchip_register(&pic_driver);
    /* 同时设置为默认(兼容旧代码) */
    irq_set_chip(&pic_chip);
}
