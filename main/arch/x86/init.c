/**
 * @file init.c
 * @brief x86 架构初始化
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <arch/cpu.h>
#include <plat/platform.h>

#include <asm/smp_defs.h>

/* GDT/IDT 初始化 (在 core 层) */
extern void gdt_init(void);
extern void idt_init(void);

/* SMP 信息 */
extern struct smp_info g_smp_info;

void arch_early_init(void) {
    /*
     * 通过平台描述符初始化早期设备
     * 包括: 控制台、中断控制器、定时器等
     */
    const struct platform_desc *plat = platform_get();
    if (plat && plat->early_init) {
        plat->early_init();
    }
    if (plat && plat->driver_init) {
        plat->driver_init();
    }
}

void arch_init(void) {
    /* 初始化 GDT/IDT */
    gdt_init();
    idt_init();

    /*
     * 外部 IRQ 先使用 8259 PIC (PIT/键盘等 ISA IRQ 更稳定)
     * LAPIC 初始化在 smp_init 中进行,用于 IPI 拉起 AP
     */
    (void)g_smp_info;
}
