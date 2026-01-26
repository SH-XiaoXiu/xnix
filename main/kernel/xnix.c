/**
 * @file xnix.c
 * @brief Xnix 内核入口
 */

#include <arch/cpu.h>
#include <arch/hal/feature.h>

#include <drivers/timer.h>

#include <asm/multiboot.h>
#include <kernel/io/ioport.h>
#include <kernel/irq/irq.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/boot.h>
#include <xnix/config.h>
#include <xnix/console.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>

/* UDM 控制台接口 */
extern void            udm_console_set_endpoint(cap_handle_t ep);
extern struct console *udm_console_get_driver(void);

static void boot_console_udm_switch(void *arg) {
    (void)arg;
    console_replace("serial", udm_console_get_driver());
    pr_ok("UDM serial console enabled");
}

static void boot_print_banner(void) {
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
}

/**
 * Early - 驱动注册,控制台,HAL 探测
 */
static void boot_phase_early(uint32_t magic, struct multiboot_info *mb_info) {
    arch_early_init();
    console_init();
    console_clear();

    boot_init(magic, mb_info);
    boot_print_banner();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        pr_warn("Invalid Multiboot magic: 0x%x", magic);
    } else {
        pr_info("Multiboot info at %p", mb_info);
    }
}

/**
 * Core - CPU 基础结构,内存,中断
 */
static void boot_phase_core(void) {
    arch_init();
    pr_ok("GDT/IDT initialized");

    mm_init();
    pr_ok("Memory manager initialized");

    irq_init();
    pr_ok("IRQ subsystem initialized");
}

/**
 * Subsys - 进程,IPC,调度器
 */
static void boot_phase_subsys(void) {
    process_init();
    pr_ok("Process manager initialized");

    ipc_init();
    pr_ok("IPC subsystem initialized");

    ioport_init();

    sched_init();
    pr_ok("Scheduler initialized");
}

/**
 * Late - 定时器
 */
static void boot_phase_late(void) {
    timer_set_callback(sched_tick);
    timer_init(CFG_SCHED_HZ);
    pr_ok("Timer initialized (%d Hz)", CFG_SCHED_HZ);
}

/**
 * Services - 仅启动 init 进程
 *
 * 内核只负责:
 * 创建必要的 cap(serial_ep, io_cap)
 * 启动 init 进程并传递这些 cap
 *
 * seriald 等服务的启动由 init 进程负责(通过 sys_spawn)
 */
static void boot_start_services(void) {
    uint32_t mods_count = boot_get_module_count();
    if (mods_count == 0) {
        pr_warn("No modules found");
        return;
    }

    /* 创建 UDM console endpoint 和 I/O port capability */
    cap_handle_t serial_ep = endpoint_create();
    cap_handle_t io_cap    = CAP_HANDLE_INVALID;

    if (serial_ep != CAP_HANDLE_INVALID) {
        udm_console_set_endpoint(serial_ep);
        io_cap = ioport_create_range((struct process *)process_current(), 0x3F8, 0x3FF,
                                     CAP_READ | CAP_WRITE | CAP_GRANT);
    }

    /* 获取 init 模块 */
    uint32_t init_mod_index = boot_get_initmod_index();
    if (init_mod_index >= mods_count) {
        pr_warn("Boot: xnix.initmod=%u out of range, defaulting to 0", init_mod_index);
        init_mod_index = 0;
    }

    void    *mod_addr = NULL;
    uint32_t mod_size = 0;
    if (boot_get_module(init_mod_index, &mod_addr, &mod_size) < 0) {
        pr_err("Failed to get init module");
        return;
    }

    pr_info("Loading init module (%u bytes)", mod_size);

    /*
     * 传递给 init 的 capability:
     *   handle 0: serial_ep (用于 printf 输出)
     *   handle 1: io_cap (传递给 seriald)
     */
    if (serial_ep != CAP_HANDLE_INVALID && io_cap != CAP_HANDLE_INVALID) {
        struct spawn_inherit_cap init_inherit[2] = {
            {.src = serial_ep, .rights = CAP_READ | CAP_WRITE | CAP_GRANT, .expected_dst = 0},
            {.src = io_cap, .rights = CAP_READ | CAP_WRITE | CAP_GRANT, .expected_dst = 1},
        };
        process_spawn_module_ex("init", mod_addr, mod_size, init_inherit, 2);

        /* UDM console 切换线程 */
        thread_create("console_udm_switch", boot_console_udm_switch, NULL);
    } else {
        process_spawn_module("init", mod_addr, mod_size);
    }
}

/**
 * 内核主入口
 */
void kernel_main(uint32_t magic, struct multiboot_info *mb_info) {
    boot_phase_early(magic, mb_info);
    boot_phase_core();
    boot_phase_subsys();
    boot_phase_late();
    boot_start_services();

    pr_info("Starting scheduler...");
    cpu_irq_enable();

    while (1) {
        cpu_halt();
    }
}
