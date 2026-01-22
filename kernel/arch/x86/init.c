/**
 * @file init.c
 * @brief x86 架构初始化
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <arch/cpu.h>

/* 驱动注册函数声明 */
extern void vga_console_register(void);
extern void serial_console_register(void);
extern void pic_register(void);
extern void pit_register(void);

/* GDT/IDT 初始化 */
extern void gdt_init(void);
extern void idt_init(void);

void arch_early_init(void) {
    /* 注册所有 x86 驱动 */
    vga_console_register();
    serial_console_register();
    pic_register();
    pit_register();
}

void arch_init(void) {
    /* 初始化 GDT/IDT */
    gdt_init();
    idt_init();
}
