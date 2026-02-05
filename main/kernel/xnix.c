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
#include <kernel/sys/syscall.h>
#include <xnix/abi/process.h>
#include <xnix/boot.h>
#include <xnix/config.h>
#include <xnix/console.h>
#include <xnix/driver.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/stdio.h>

#include "xnix/debug.h"

/* input_init stub (now in sys_input.c) */
extern void input_init(void);

static void boot_print_banner(void) {
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

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        pr_warn("Invalid Multiboot magic: 0x%x", magic);
    } else {
        pr_info("Multiboot info at %p", mb_info);
    }
}

/**
 * Core - CPU 基础结构,内存,中断
 */
/* Framebuffer 延迟初始化(VMM 就绪后) */
extern void fb_late_init(void);

static void boot_phase_core(void) {
    arch_init();
    pr_ok("GDT/IDT");

    mm_init();
    pr_ok("Memory manager.");

    /* VMM 已就绪,现在可以映射 framebuffer */
    fb_late_init();

    hal_probe_smp_late();
    boot_print_banner();

    /* Boot 裁切:根据命令行选择 IRQ 控制器 */
    const char *irqchip_prefer = boot_get_cmdline_value("xnix.irqchip");
    irqchip_select_and_init(irqchip_prefer);

    irq_init();
    pr_ok("IRQ subsystem.");
}

/**
 * Subsys - 进程,IPC,调度器
 */
static void boot_phase_subsys(void) {
    process_init();
    pr_ok("Process manager.");

    ipc_init();
    pr_ok("IPC subsystem.");

    perm_init();
    ioport_init();
    input_init();

    syscall_init();

    sched_init();
    pr_ok("Scheduler.");
}

/**
 * SMP - 切换到 APIC 并启动其他 CPU 核心
 */
static void boot_phase_smp(void) {
    arch_smp_init();
}

/**
 * Late - 定时器,异步输出
 */
static void boot_phase_late(void) {
    /* Boot 裁切:根据命令行选择定时器 */
    const char *timer_prefer = boot_get_cmdline_value("xnix.timer");
    timer_drv_select_best(timer_prefer);

    timer_set_callback(sched_tick);
    timer_init(CFG_SCHED_HZ);
    pr_ok("Timer (%d Hz)", CFG_SCHED_HZ);

    /* 启动异步消费者线程并启用异步输出 */
    console_start_consumers();
    console_async_enable();
    pr_ok("Async console output enabled");

    /* 收集启动资源并创建 handles */
    bootinfo_collect();
}

/**
 * Services - 仅启动 init 进程
 *
 * 内核只负责:
 * 启动 init 进程
 *
 * seriald,ramfsd,fatfsd 等服务的启动由 init 进程负责(通过 sys_spawn)
 */
static void boot_start_services(void) {
    uint32_t mods_count = boot_get_module_count();
    if (mods_count == 0) {
        pr_warn("No modules found");
        return;
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

    /* 解析 init module 的 cmdline 为 argc/argv */
    const char *init_cmdline = boot_get_module_cmdline(init_mod_index);
    int         init_argc    = 0;
    char        init_argv[ABI_EXEC_MAX_ARGS][ABI_EXEC_MAX_ARG_LEN];

    if (init_cmdline && init_cmdline[0]) {
        pr_info("Init cmdline: %s", init_cmdline);

        /* 解析空格分隔的参数 */
        const char *p = init_cmdline;
        while (*p && init_argc < ABI_EXEC_MAX_ARGS) {
            while (*p == ' ') {
                p++;
            }
            if (*p == '\0') {
                break;
            }

            /* 提取一个参数 */
            int i = 0;
            while (*p && *p != ' ' && i < ABI_EXEC_MAX_ARG_LEN - 1) {
                init_argv[init_argc][i++] = *p++;
            }
            init_argv[init_argc][i] = '\0';
            init_argc++;
        }
    }

    /* 获取 boot handles 传递给 init */
    struct spawn_handle *boot_handles      = NULL;
    uint32_t             boot_handle_count = 0;
    if (bootinfo_get_handles(&boot_handles, &boot_handle_count) < 0) {
        pr_warn("Failed to get boot handles, init will start without handles");
    } else {
        pr_info("Passing %u boot handles to init", boot_handle_count);
    }

    pid_t                init_pid     = PID_INVALID;
    struct perm_profile *init_profile = perm_profile_find("init");
    if (!init_profile) {
        pr_warn("Init profile not found, spawning init without profile");
    }
    if (init_argc > 0) {
        init_pid = process_spawn_module_ex_with_args("init", mod_addr, mod_size, boot_handles,
                                                     boot_handle_count, init_profile, init_argc,
                                                     init_argv);
    } else {
        init_pid = process_spawn_module_ex("init", mod_addr, mod_size, boot_handles,
                                           boot_handle_count, init_profile);
    }

    if (init_pid == PID_INVALID) {
        pr_err("Failed to spawn init");
        return;
    }
    if (init_pid != XNIX_PID_INIT) {
        panic("Init PID mismatch: expected %d, got %d", XNIX_PID_INIT, init_pid);
    }
}

/**
 * 内核主入口
 */
void kernel_main(uint32_t magic, struct multiboot_info *mb_info) {
    boot_phase_early(magic, mb_info);
    boot_phase_core();
    boot_phase_subsys();
    boot_phase_smp();
    boot_phase_late();
    boot_start_services();

    pr_info("Starting scheduler...");
    cpu_irq_enable();
    schedule();
    __builtin_unreachable();
}
