/**
 * @file xnix.c
 * @brief Xnix 内核入口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <arch/cpu.h>

#include <drivers/console.h>
#include <drivers/irqchip.h>
#include <drivers/timer.h>

#include <xnix/stdio.h>

#include "sched/sched.h"

/* 测试任务 A */
static void task_a(void) {
    while (1) {
        kprintf("%R[A]%N Running...\n");
        for (volatile int i = 0; i < 100000000; i++);
    }
}

/* 测试任务 B */
static void task_b(void) {
    while (1) {
        kprintf("%B[B]%N Running...\n");
        for (volatile int i = 0; i < 200000000; i++);
    }
}

void kernel_main(void) {
    /* 注册所有驱动 */
    arch_early_init();

    /* 初始化控制台 */
    console_init();
    console_clear();

    kprintf("\n");
    kprintf("%C========================================%N\n");
    kprintf("%C        Xnix Kernel Loaded!%N\n");
    kprintf("%C========================================%N\n");
    kprintf("\n");

    /* 初始化架构（GDT/IDT） */
    arch_init();
    kprintf("%G[OK]%N GDT/IDT initialized\n");

    /* 初始化中断控制器 */
    irqchip_init();
    kprintf("%G[OK]%N IRQ chip initialized\n");

    /* 初始化调度器 */
    sched_init();
    sched_create(task_a);
    sched_create(task_b);
    kprintf("%G[OK]%N Scheduler initialized\n");

    /* 设置定时器回调并初始化 */
    timer_set_callback(sched_tick);
    timer_init(10); /* 10 Hz */
    kprintf("%G[OK]%N Timer initialized (10 Hz)\n");

    /* 开启中断 */
    kprintf("%Y[INFO]%N Enabling interrupts...\n");
    cpu_irq_enable();

    /* 主循环 */
    while (1) {
        cpu_halt();
    }
}
