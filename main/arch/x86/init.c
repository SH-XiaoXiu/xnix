/**
 * @file init.c
 * @brief x86 架构初始化
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <arch/cpu.h>

#include <asm/smp_defs.h>

/* 驱动注册函数声明 */
extern void vga_console_register(void);
extern void serial_console_register(void);
extern void pic_register(void);
extern void pit_register(void);
extern void apic_register(void);

/* GDT/IDT 初始化 */
extern void gdt_init(void);
extern void idt_init(void);

/* SMP 信息 */
extern struct smp_info g_smp_info;

void arch_early_init(void) {
    /* 注册控制台驱动 */
    vga_console_register();
    serial_console_register();

    /*
     * 中断控制器驱动在此注册 (PIC 作为默认)
     * APIC 驱动在 hal_probe_features 解析 MP Table 后,
     * 由 arch_init 决定是否切换
     */
    pic_register();
    pit_register();
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
