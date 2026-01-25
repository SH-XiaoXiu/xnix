/**
 * @file xnix.c
 * @brief Xnix 内核入口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <arch/cpu.h>
#include <arch/hal/feature.h>

#include <drivers/timer.h>

#include <asm/multiboot.h>
#include <kernel/irq/irq.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/boot.h>
#include <xnix/config.h>
#include <xnix/console.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>

/* 测试任务 bg */
static void task_bg(void *arg) {
    (void)arg;
    while (1) {
        kprintf("%N[BG]%N Running...\n");
        sleep_ms(2000);
    }
}

extern cap_handle_t    serial_udm_start(void);
extern struct console *serial_udm_console_driver(void);

static void boot_console_udm_switch(void *arg) {
    (void)arg;
    console_replace("serial", serial_udm_console_driver());
    pr_ok("UDM serial console enabled");
}

void kernel_main(uint32_t magic, struct multiboot_info *mb_info) {
    /* 注册所有驱动 */
    arch_early_init();

    /* 初始化控制台 */
    console_init();
    console_clear();

    boot_init(magic, mb_info);

    extern struct hal_features g_hal_features;
    kprintf("\n");
    kprintf("%C========================================%N\n");
    kprintf("%C        Xnix Kernel Loaded!%N\n");
    kprintf("%C========================================%N\n");
    kprintf("Detected CPU: %s (%d cores)\n", g_hal_features.cpu_vendor, g_hal_features.cpu_count);
    kprintf("Features: [MMU:%s] [FPU:%s] [SMP:%s]\n",
            (g_hal_features.flags & HAL_FEATURE_MMU) ? "Yes" : "No",
            (g_hal_features.flags & HAL_FEATURE_FPU) ? "Yes" : "No",
            (g_hal_features.flags & HAL_FEATURE_SMP) ? "Yes" : "No");
    if (g_hal_features.ram_size_mb) {
        kprintf("RAM: %u MB\n", g_hal_features.ram_size_mb);
    }
    kprintf("\n");

    /* 检查 Multiboot 魔数 */
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        pr_warn("Invalid Multiboot magic: 0x%x", magic);
    } else {
        pr_info("Multiboot info at %p", mb_info);
    }

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

    cap_handle_t serial_ep = serial_udm_start();
    if (serial_ep != CAP_HANDLE_INVALID) {
        process_set_boot_console_endpoint(serial_ep);
        thread_create("console_udm_switch", boot_console_udm_switch, NULL);
    }

    thread_create("task_bg", task_bg, NULL);
    pr_info("Threads created");

    /* 设置定时器回调并初始化 */
    timer_set_callback(sched_tick);
    timer_init(CFG_SCHED_HZ); /* 使用配置的频率 */
    pr_ok("Timer initialized (%d Hz)", CFG_SCHED_HZ);

    /* 查找/启动 init 进程 */
    struct multiboot_mod_list *init_mod       = NULL;
    uint32_t                   init_mod_index = 0;
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mb_info && (mb_info->flags & MULTIBOOT_INFO_MODS)) {
        if (mb_info->mods_count > 0) {
            init_mod = (struct multiboot_mod_list *)mb_info->mods_addr;
            pr_info("Found init module at %p - %p", init_mod->mod_start, init_mod->mod_end);
        }
    }
    if (init_mod) {
        /* 将模块地址转换为内核虚拟地址 */
        /* Multiboot 模块地址是物理地址 */
        /* 内核建立了 1:1 映射 (当前 VMM 实现已经映射了所有物理内存 128MB) */
        /* 注意: VMM 初始映射只覆盖了有限的内存*/
        /* 需要确保模块地址在映射范围内 */
        /* GRUB 加载的模块通常在低端内存 */
        /*
           注意: multiboot_info 中的 mods_addr 指向的是 multiboot_mod_list 数组
           这个数组本身可能在任何位置.
           init_mod->mod_start 才是模块本身的物理地址.
        */
        /* 确保我们访问的是正确的指针 */
        struct multiboot_mod_list *mods = (struct multiboot_mod_list *)mb_info->mods_addr;
        init_mod_index                  = boot_get_initmod_index();
        if (init_mod_index >= mb_info->mods_count) {
            pr_warn("Boot: xnix.initmod=%u out of range, defaulting to 0", init_mod_index);
            init_mod_index = 0;
        }
        init_mod           = &mods[init_mod_index];
        void    *mod_vaddr = (void *)init_mod->mod_start;
        uint32_t mod_size  = init_mod->mod_end - init_mod->mod_start;
        pr_info("Found init module (%u bytes)", mod_size);
        process_spawn_init(mod_vaddr, mod_size);
    } else {
        pr_warn("No init module found");
    }
    pr_info("Starting scheduler...");
    cpu_irq_enable();
    while (1) {
        cpu_halt();
    }
}
