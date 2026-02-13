/**
 * @file xnix.c
 * @brief Xnix 内核入口
 */

#include <arch/cpu.h>
#include <arch/hal/feature.h>

#include <drivers/timer.h>

#include <asm/multiboot.h>
#include <io/ioport.h>
#include <sys/syscall.h>
#include <xnix/abi/process.h>
#include <xnix/boot.h>
#include <xnix/config.h>
#include <xnix/driver.h>
#include <xnix/early_console.h>
#include <xnix/ipc.h>
#include <xnix/irq.h>
#include <xnix/kmsg.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/thread_def.h>

#include "xnix/debug.h"

/* input_init stub (now in sys_input.c) */
extern void input_init(void);

static void boot_print_banner(void) {
    kprintf("\n");
    kprintf("========================================\n");
    kprintf("        Xnix Kernel Loaded!\n");
    kprintf("========================================\n");
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
    kmsg_init();
    early_console_init();
    early_clear();

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
    const char *irqchip_prefer = boot_cmdline_get("xnix.irqchip");
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
 * Late - 定时器
 */
static void boot_phase_late(void) {
    /* Boot 裁切:根据命令行选择定时器 */
    const char *timer_prefer = boot_cmdline_get("xnix.timer");
    timer_drv_select_best(timer_prefer);

    timer_set_callback(sched_tick);
    timer_init(CFG_SCHED_HZ);
    pr_ok("Timer (%d Hz)", CFG_SCHED_HZ);

    /* 收集启动资源并创建 handles */
    boot_handles_collect();
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
    const char *init_name = boot_cmdline_get("xnix.init");
    if (!init_name || !init_name[0]) {
        init_name = "init";
    }

    void    *mod_addr = NULL;
    uint32_t mod_size = 0;
    if (boot_find_module_by_name(init_name, &mod_addr, &mod_size) < 0) {
        pr_err("Failed to find init module: %s", init_name);
        return;
    }

    pr_info("Loading init module '%s' (%u bytes)", init_name, mod_size);

    /* 解析 init module 的 cmdline 为 argc/argv */
    const char *init_cmdline = boot_get_module_cmdline_by_name(init_name);
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
            if (strncmp(init_argv[init_argc], "name=", 5) != 0) {
                init_argc++;
            }
        }
    }

    pid_t init_pid = PID_INVALID;
    if (init_argc > 0) {
        init_pid = process_spawn_elf_ex_with_args_flags("init", mod_addr, mod_size, NULL, 0, NULL,
                                                        init_argc, init_argv, ABI_EXEC_INHERIT_ALL);
    } else {
        init_pid = process_spawn_elf_ex_with_args_flags("init", mod_addr, mod_size, NULL, 0, NULL,
                                                        0, NULL, ABI_EXEC_INHERIT_ALL);
    }

    /* 为 init 直接授予全权限(不依赖 named profile) */
    if (init_pid != PID_INVALID) {
        struct process *init_proc = process_find_by_pid(init_pid);
        if (init_proc && init_proc->perms) {
            perm_grant(init_proc->perms, "xnix.*");
        }
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
