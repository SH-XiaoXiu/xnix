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

#include <xnix/config.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/thread.h>

/* 测试任务 A */
static void task_a(void *arg) {
    (void)arg;
    while (1) {
        kprintf("%R[A]%N Running...\n");
        sleep_ms(1000);
    }
}

/* 测试任务 B */
static void task_b(void *arg) {
    (void)arg;
    while (1) {
        kprintf("%B[B]%N Running...\n");
        sleep_ms(5000);
    }
}

/* 内存测试任务:分配和释放内存 */
static void task_memtest(void *arg) {
    (void)arg;
    int round = 0;
    while (1) {
        kprintf("%Y[MemTest]%N Round %d: ", round++);

        /* 分配几个页 */
        void *p1 = alloc_page();
        void *p2 = alloc_page();
        void *p3 = alloc_pages(2);
        kprintf("alloc p1=%p p2=%p p3=%p, ", p1, p2, p3);

        mm_dump_stats();

        /* 释放 */
        free_page(p1);
        free_page(p2);
        free_pages(p3, 2);
        kprintf("%Y[MemTest]%N freed, ");
        mm_dump_stats();

        sleep_ms(1000);
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

    /* 初始化架构(GDT/IDT) */
    arch_init();
    kprintf("%G[OK]%N GDT/IDT initialized\n");

    /* 初始化内存管理 */
    mm_init();
    kprintf("%G[OK]%N Memory manager initialized\n");

    /* 初始化中断控制器 */
    irqchip_init();
    kprintf("%G[OK]%N IRQ chip initialized\n");

    /* 初始化进程管理 */
    process_init();
    kprintf("%G[OK]%N Process manager initialized\n");

    /* 初始化 IPC 子系统 */
    ipc_init();
    kprintf("%G[OK]%N IPC subsystem initialized\n");

    /* 初始化调度器 */
    sched_init();
    thread_create("memtest", task_memtest, NULL);
    thread_create("task_a", task_a, NULL);
    thread_create("task_b", task_b, NULL);
    kprintf("%G[OK]%N Threads created\n");

    /* 设置定时器回调并初始化 */
    timer_set_callback(sched_tick);
    timer_init(CFG_SCHED_HZ); /* 使用配置的频率 */
    kprintf("%G[OK]%N Timer initialized (%d Hz)\n", CFG_SCHED_HZ);

    /* 开启中断 */
    kprintf("%Y[INFO]%N Enabling interrupts...\n");
    cpu_irq_enable();

    /* 主循环 */
    while (1) {
        cpu_halt();
    }
}
