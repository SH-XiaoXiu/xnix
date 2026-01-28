/**
 * @file smp_init.c
 * @brief SMP 初始化实现
 *
 * 负责启动所有 AP 核心
 */

#include <arch/cpu.h>
#include <arch/smp.h>

#include <asm/apic.h>
#include <asm/smp_asm.h>
#include <asm/smp_defs.h>
#include <xnix/config.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* Trampoline 代码标记 (在 ap_trampoline.s 中定义) */
extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];

/* Trampoline 中的变量 (由 BSP 填充) */
extern uint32_t ap_stacks[];
extern uint8_t  ap_lapic_ids[];

/* 内核 GDT 指针 (在 trampoline 中) */
extern struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) ap_kernel_gdtr;

extern struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) ap_kernel_idtr;

extern uint32_t ap_kernel_cr3;

/* SMP 信息 */
extern struct smp_info g_smp_info;

/* Per-CPU 数据 */
extern struct per_cpu_data g_per_cpu[];

/* 设置在线状态 */
extern void cpu_set_online(cpu_id_t cpu, bool online);

/* AP_TRAMPOLINE_ADDR 定义在 <asm/smp_asm.h> 中 */

/* AP 启动超时 (毫秒) */
#define AP_STARTUP_TIMEOUT_MS 100

/* 简单延迟函数 (使用 PIT) */
static void delay_us(uint32_t us) {
    /* 使用 I/O 端口延迟, 每次约 1us */
    for (uint32_t i = 0; i < us; i++) {
        inb(0x80);
    }
}

static void delay_ms(uint32_t ms) {
    delay_us(ms * 1000);
}

/**
 * 复制 trampoline 代码到低端内存
 */
static void smp_copy_trampoline(void) {
    size_t trampoline_size = ap_trampoline_end - ap_trampoline_start;
    memcpy((void *)AP_TRAMPOLINE_ADDR, ap_trampoline_start, trampoline_size);
}

/**
 * 设置 trampoline 中的变量
 */
static void smp_setup_trampoline(void) {
    /* 计算 trampoline 中变量的实际地址 */
    uint32_t *stacks_ptr =
        (uint32_t *)(AP_TRAMPOLINE_ADDR + ((uint32_t)ap_stacks - (uint32_t)ap_trampoline_start));
    uint8_t *lapic_ids_ptr =
        (uint8_t *)(AP_TRAMPOLINE_ADDR + ((uint32_t)ap_lapic_ids - (uint32_t)ap_trampoline_start));

    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) *gdtr_ptr =
        (void *)(AP_TRAMPOLINE_ADDR + ((uint32_t)&ap_kernel_gdtr - (uint32_t)ap_trampoline_start));

    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) *idtr_ptr =
        (void *)(AP_TRAMPOLINE_ADDR + ((uint32_t)&ap_kernel_idtr - (uint32_t)ap_trampoline_start));

    uint32_t *cr3_ptr = (uint32_t *)(AP_TRAMPOLINE_ADDR +
                                     ((uint32_t)&ap_kernel_cr3 - (uint32_t)ap_trampoline_start));

    /* 直接读取 GDTR */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) current_gdtr;
    __asm__ volatile("sgdt %0" : "=m"(current_gdtr));

    gdtr_ptr->limit = current_gdtr.limit;
    gdtr_ptr->base  = current_gdtr.base;

    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) current_idtr;
    __asm__ volatile("sidt %0" : "=m"(current_idtr));
    idtr_ptr->limit = current_idtr.limit;
    idtr_ptr->base  = current_idtr.base;

    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    *cr3_ptr = cr3;

    /* 填充 LAPIC ID 数组 */
    for (uint32_t i = 0; i < g_smp_info.cpu_count && i < CFG_MAX_CPUS; i++) {
        lapic_ids_ptr[i] = g_smp_info.lapic_ids[i];
    }

    /* 为每个 AP 分配栈 */
    for (uint32_t i = 0; i < g_smp_info.cpu_count && i < CFG_MAX_CPUS; i++) {
        if (i == g_smp_info.bsp_id) {
            /* BSP 使用现有栈 */
            stacks_ptr[i] = 0;
            continue;
        }

        /* 分配 AP 栈 (4KB) */
        void *stack = kmalloc(4096);
        if (!stack) {
            pr_err("SMP: Failed to allocate stack for CPU%u", i);
            continue;
        }

        /* 栈指针指向栈顶 */
        stacks_ptr[i]          = (uint32_t)stack + 4096;
        g_per_cpu[i].int_stack = (uint32_t *)stacks_ptr[i];
    }
}

/**
 * 启动单个 AP
 *
 * @param cpu 逻辑 CPU ID
 * @return 成功返回 0
 */
static int smp_start_ap(uint32_t cpu) {
    if (cpu >= g_smp_info.cpu_count || cpu == g_smp_info.bsp_id) {
        return -1;
    }

    uint8_t lapic_id = g_smp_info.lapic_ids[cpu];

    /* 初始化 Per-CPU 数据 */
    g_per_cpu[cpu].cpu_id   = cpu;
    g_per_cpu[cpu].lapic_id = lapic_id;
    g_per_cpu[cpu].started  = false;
    g_per_cpu[cpu].ready    = false;

    /* 发送 INIT IPI */
    lapic_send_init(lapic_id);

    /* 等待 10ms */
    delay_ms(10);

    /* 发送 INIT deassert (仅对旧处理器需要) */
    lapic_send_init_deassert();

    /* 发送两次 SIPI (Startup IPI) */
    /* 向量 = trampoline 地址 / 4KB = 0x8000 / 0x1000 = 0x08 */
    uint8_t vector = AP_TRAMPOLINE_ADDR >> 12;

    for (int i = 0; i < 2; i++) {
        lapic_send_sipi(lapic_id, vector);
        delay_us(200); /* 等待 200us */
    }

    /* 等待 AP 启动 */
    uint32_t timeout = AP_STARTUP_TIMEOUT_MS;
    while (!g_per_cpu[cpu].started && timeout > 0) {
        delay_ms(1);
        timeout--;
    }

    if (!g_per_cpu[cpu].started) {
        pr_err("SMP: CPU%u failed to start", cpu);
        return -1;
    }

    pr_ok("CPU%u online", cpu);
    cpu_set_online(cpu, true);

    return 0;
}

/* GDT AP 初始化 */
extern void gdt_init_ap(uint32_t cpu_id);

/* 调度器入口 */
extern void schedule(void);

/**
 * AP 入口函数 (由 trampoline 调用)
 *
 * @param cpu_id 逻辑 CPU ID
 */
void ap_main(uint32_t cpu_id) {
    /* 初始化本 CPU 的 GDT/TSS */
    gdt_init_ap(cpu_id);

    /* 初始化本地 LAPIC */
    lapic_init();

    /* 初始化本 CPU 的 LAPIC 定时器 */
    lapic_timer_init(CFG_SCHED_HZ);

    /* 标记已启动 */
    g_per_cpu[cpu_id].started = true;

    /* 等待 BSP 完成所有初始化 */
    while (!g_per_cpu[cpu_id].ready) {
        cpu_pause();
    }

    /* 启用中断并进入调度循环 */
    pr_info("CPU%u entering scheduler", cpu_id);
    cpu_irq_enable();
    schedule();

    /* 不应到达这里 */
    __builtin_unreachable();
}

/**
 * SMP 初始化入口
 *
 * 启动所有 AP 核心,此函数作为强符号覆盖 lib/arch_stubs.c 中的弱符号
 */
void arch_smp_init(void) {
    if (g_smp_info.cpu_count <= 1) {
        return;
    }

    if (!g_smp_info.apic_available) {
        pr_warn("SMP: APIC not available");
        return;
    }

    /* 外部 IRQ 可能仍由 PIC 负责,但拉起 AP 需要 LAPIC 可用 */
    lapic_init();

    pr_info("SMP: Starting %u CPUs...", g_smp_info.cpu_count);
    pr_info("SMP: bsp_id=%u", g_smp_info.bsp_id);
    for (uint32_t i = 0; i < g_smp_info.cpu_count; i++) {
        pr_info("SMP: cpu%u lapic_id=%u", i, g_smp_info.lapic_ids[i]);
    }

    /* 初始化 BSP 的 Per-CPU 数据 */
    g_per_cpu[g_smp_info.bsp_id].cpu_id   = g_smp_info.bsp_id;
    g_per_cpu[g_smp_info.bsp_id].lapic_id = g_smp_info.lapic_ids[g_smp_info.bsp_id];
    g_per_cpu[g_smp_info.bsp_id].started  = true;
    g_per_cpu[g_smp_info.bsp_id].ready    = true;
    cpu_set_online(g_smp_info.bsp_id, true);

    /* 复制并设置 trampoline */
    smp_copy_trampoline();
    smp_setup_trampoline();

    /* 启动所有 AP */
    uint32_t online_count = 1; /* BSP 已经在线 */
    for (uint32_t i = 0; i < g_smp_info.cpu_count; i++) {
        if (i == g_smp_info.bsp_id) {
            continue;
        }

        if (smp_start_ap(i) == 0) {
            online_count++;
        }
    }

    /* 标记所有 AP 就绪 */
    for (uint32_t i = 0; i < g_smp_info.cpu_count; i++) {
        if (i != g_smp_info.bsp_id && g_per_cpu[i].started) {
            g_per_cpu[i].ready = true;
        }
    }

    pr_ok("SMP: %u CPUs ready", online_count);
}

/* 保留旧名称以兼容可能的外部引用 */
__attribute__((alias("arch_smp_init"))) void smp_init(void);
