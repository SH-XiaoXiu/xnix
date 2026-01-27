/**
 * @file ioapic.c
 * @brief x86 I/O APIC 驱动
 *
 * I/O APIC 负责将外部中断路由到各个 CPU 的 LAPIC
 */

#include <asm/apic.h>
#include <asm/smp_defs.h>

#include <arch/cpu.h>
#include <kernel/irq/irq.h>
#include <xnix/stdio.h>

/* I/O APIC 映射的虚拟地址 */
static volatile uint32_t *ioapic_base = NULL;

/* 最大中断输入数量 */
static uint8_t ioapic_max_redir = 0;

uint32_t ioapic_read(uint8_t reg) {
    if (!ioapic_base) {
        return 0;
    }
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_REGWIN / 4];
}

void ioapic_write(uint8_t reg, uint32_t val) {
    if (!ioapic_base) {
        return;
    }
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_REGWIN / 4] = val;
}

void ioapic_set_base(paddr_t base) {
    ioapic_base = (volatile uint32_t *)base;
}

/**
 * 读取重定向表项
 */
static uint64_t ioapic_read_redir(uint8_t irq) {
    uint32_t lo = ioapic_read(IOAPIC_REDTBL + 2 * irq);
    uint32_t hi = ioapic_read(IOAPIC_REDTBL + 2 * irq + 1);
    return ((uint64_t)hi << 32) | lo;
}

/**
 * 写入重定向表项
 */
static void ioapic_write_redir(uint8_t irq, uint64_t val) {
    ioapic_write(IOAPIC_REDTBL + 2 * irq, (uint32_t)val);
    ioapic_write(IOAPIC_REDTBL + 2 * irq + 1, (uint32_t)(val >> 32));
}

void ioapic_init(void) {
    extern struct smp_info g_smp_info;

    if (!g_smp_info.apic_available) {
        return;
    }

    /* 设置 I/O APIC 基地址 */
    ioapic_base = (volatile uint32_t *)g_smp_info.ioapic_base;
    if (!ioapic_base) {
        ioapic_base = (volatile uint32_t *)IOAPIC_BASE_DEFAULT;
    }

    /* 读取 I/O APIC 版本和最大重定向条目 */
    uint32_t ver = ioapic_read(IOAPIC_VER);
    ioapic_max_redir = ((ver >> 16) & 0xFF) + 1;

    pr_debug("IOAPIC: ID=%u, ver=%u, max_irq=%u",
             ioapic_read(IOAPIC_ID) >> 24, ver & 0xFF, ioapic_max_redir);

    /* 屏蔽所有中断 */
    for (uint8_t i = 0; i < ioapic_max_redir; i++) {
        ioapic_write_redir(i, IOAPIC_INT_MASKED);
    }

    pr_ok("IOAPIC: Initialized at 0x%x", (uint32_t)ioapic_base);
}

void ioapic_enable_irq(uint8_t irq, uint8_t vector, uint8_t dest) {
    if (!ioapic_base || irq >= ioapic_max_redir) {
        return;
    }

    /*
     * 重定向表项格式 (64-bit):
     * [7:0]   - 向量号
     * [10:8]  - 交付模式 (000=Fixed)
     * [11]    - 目标模式 (0=物理, 1=逻辑)
     * [12]    - 交付状态 (只读)
     * [13]    - 中断极性 (0=高电平有效, 1=低电平有效)
     * [14]    - 远程 IRR (只读)
     * [15]    - 触发模式 (0=边沿, 1=电平)
     * [16]    - 屏蔽位
     * [55:17] - 保留
     * [63:56] - 目标 LAPIC ID (物理模式)
     */
    uint64_t redir = vector;
    redir |= ((uint64_t)dest << 56); /* 目标 LAPIC ID */

    /* ISA 中断默认配置: 边沿触发, 高电平有效 */
    /* 某些 IRQ (如 PCI) 可能需要电平触发 */

    ioapic_write_redir(irq, redir);

    pr_debug("IOAPIC: IRQ%u -> vector 0x%02x, dest=%u", irq, vector, dest);
}

void ioapic_disable_irq(uint8_t irq) {
    if (!ioapic_base || irq >= ioapic_max_redir) {
        return;
    }

    uint64_t redir = ioapic_read_redir(irq);
    redir |= IOAPIC_INT_MASKED;
    ioapic_write_redir(irq, redir);
}

/*
 * APIC 中断芯片操作 (替换 8259 PIC)
 */

static void apic_chip_init(void) {
    /* 禁用 8259 PIC */
    outb(0x21, 0xFF); /* PIC1 */
    outb(0xA1, 0xFF); /* PIC2 */

    /* 初始化 LAPIC 和 I/O APIC */
    lapic_init();
    ioapic_init();
}

static void apic_chip_enable(uint8_t irq) {
    extern struct smp_info g_smp_info;

    if (!g_smp_info.apic_available) {
        return;
    }

    /* 将 IRQ 路由到 BSP (LAPIC ID 0) */
    /* 中断向量 = 0x20 + IRQ (与 PIC 兼容) */
    uint8_t vector = 0x20 + irq;
    uint8_t dest = g_smp_info.lapic_ids[0]; /* BSP */

    ioapic_enable_irq(irq, vector, dest);
}

static void apic_chip_disable(uint8_t irq) {
    ioapic_disable_irq(irq);
}

static void apic_chip_eoi(uint8_t irq) {
    (void)irq;
    lapic_eoi();
}

static const struct irqchip_ops apic_chip = {
    .name    = "apic",
    .init    = apic_chip_init,
    .enable  = apic_chip_enable,
    .disable = apic_chip_disable,
    .eoi     = apic_chip_eoi,
};

void apic_register(void) {
    extern struct smp_info g_smp_info;

    if (g_smp_info.apic_available) {
        irq_set_chip(&apic_chip);
        pr_debug("APIC: Registered as IRQ chip");
    }
}
