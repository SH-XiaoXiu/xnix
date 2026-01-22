/**
 * @file xnix.c
 * @brief Xnix 内核入口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <xstd/stdio.h>
#include <arch/console.h>
#include <arch/cpu.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <arch/pic.h>
#include <arch/pit.h>
#include "sched/sched.h"

/* 测试任务 A */
static void task_a(void) {
    while (1) {
        kprintf("A Running...\n");
        for (volatile int i = 0; i < 100000000; i++);
    }
}

/* 测试任务 B */
static void task_b(void) {
    while (1) {
        kprintf("B Running...\n");
        for (volatile int i = 0; i < 200000000; i++);
    }
}

void kernel_main(void) {
    arch_console_init();

    kprintf("\n");
    kprintf("========================================\n");
    kprintf("        Xnix Kernel Loaded!\n");
    kprintf("========================================\n");
    kprintf("\n");

    /* 初始化 GDT */
    gdt_init();
    kprintf("GDT initialized\n");

    /* 初始化中断 */
    pic_init();
    idt_init();
    kprintf("IDT initialized\n");

    /* 初始化调度器 */
    sched_init();
    sched_create(task_a);
    sched_create(task_b);

    /* 初始化定时器并开启中断 */
    pit_init(10);  /* 10 Hz */

    kprintf("Enabling interrupts...\n");
    __asm__ volatile("sti");

    /* 主循环 */
    while (1) {
        arch_halt();
    }
}
