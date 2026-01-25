/**
 * @file xnix.c
 * @brief Xnix 内核入口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <arch/cpu.h>

#include <drivers/console.h>
#include <drivers/timer.h>

#include <kernel/irq/irq.h>
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

/*
 * IPC 广播测试
 */
static cap_handle_t g_test_notif;

static void task_ipc_worker(void *arg) {
    int id = (int)(long)arg;
    kprintf("%C[Worker %d]%N Started, waiting for notification...\n", id);

    while (1) {
        uint32_t bits = notification_wait(g_test_notif);
        kprintf("%C[Worker %d]%N Woke up! Received bits: 0x%x\n", id, bits);

        if (bits & 0x80) {
            kprintf("%C[Worker %d]%N Received exit signal, bye!\n", id);
            break;
        }
    }
}

static void task_ipc_master(void *arg) {
    (void)arg;
    kprintf("%M[Master]%N Started. Will signal workers in 3 seconds...\n");
    sleep_ms(3000);

    kprintf("%M[Master]%N Broadcasting signal 0x01...\n");
    notification_signal(g_test_notif, 0x01);
    sleep_ms(2000);

    kprintf("%M[Master]%N Broadcasting signal 0x02...\n");
    notification_signal(g_test_notif, 0x02);
    sleep_ms(2000);

    kprintf("%M[Master]%N Broadcasting signal 0xFF (Exit)...\n");
    notification_signal(g_test_notif, 0xFF);

    kprintf("%M[Master]%N Test finished.\n");
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
    pr_ok("GDT/IDT initialized");

    /* 初始化内存管理 */
    mm_init();
    pr_ok("Memory manager initialized");

    /* 初始化中断控制器 */
    irq_init();
    pr_ok("IRQ subsystem initialized");

    /* 初始化进程管理 */
    process_init();
    pr_ok("Process manager initialized");

    /* 初始化 IPC 子系统 */
    ipc_init();
    pr_ok("IPC subsystem initialized");

    /* 初始化调度器 */
    sched_init();
    pr_ok("Scheduler initialized");

    /* 创建 IPC 测试对象 */
    g_test_notif = notification_create();
    if (g_test_notif != CAP_HANDLE_INVALID) {
        thread_create("ipc_worker_1", task_ipc_worker, (void *)1);
        thread_create("ipc_worker_2", task_ipc_worker, (void *)2);
        thread_create("ipc_worker_3", task_ipc_worker, (void *)3);
        thread_create("ipc_master", task_ipc_master, NULL);
    } else {
        pr_err("Failed to create notification object!");
    }

    // thread_create("memtest", task_memtest, NULL);
    thread_create("task_a", task_a, NULL);
    thread_create("task_b", task_b, NULL);
    pr_info("Threads created");

    /* 设置定时器回调并初始化 */
    timer_set_callback(sched_tick);
    timer_init(CFG_SCHED_HZ); /* 使用配置的频率 */
    pr_ok("Timer initialized (%d Hz)", CFG_SCHED_HZ);

    /* 开启中断 */
    pr_info("Enabling interrupts...");
    cpu_irq_enable();

    /* 主循环 */
    while (1) {
        cpu_halt();
    }
}
