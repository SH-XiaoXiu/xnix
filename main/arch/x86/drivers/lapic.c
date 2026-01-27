/**
 * @file lapic.c
 * @brief x86 Local APIC 驱动
 *
 * LAPIC 提供:
 * - 本地定时器中断
 * - 核间中断 (IPI) 发送
 * - 中断优先级管理
 */

#include <asm/apic.h>
#include <asm/smp_defs.h>

#include <arch/cpu.h>
#include <xnix/debug.h>
#include <xnix/stdio.h>

/* LAPIC 映射的虚拟地址 (使用恒等映射) */
static volatile uint32_t *lapic_base = NULL;

/* 全局 SMP 信息 (由 MP Table 解析填充) */
struct smp_info g_smp_info = {0};

/* Per-CPU 数据 */
struct per_cpu_data g_per_cpu[CFG_MAX_CPUS] = {0};

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) {
        return 0;
    }
    return lapic_base[reg / 4];
}

void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base) {
        return;
    }
    lapic_base[reg / 4] = val;
    /* 读回以确保写入完成 */
    (void)lapic_base[LAPIC_ID / 4];
}

uint8_t lapic_get_id(void) {
    if (!lapic_base) {
        return 0;
    }
    return (uint8_t)(lapic_read(LAPIC_ID) >> 24);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

void lapic_init(void) {
    if (!g_smp_info.apic_available) {
        pr_warn("LAPIC: APIC not available, using legacy PIC");
        return;
    }

    /* 设置 LAPIC 基地址 (恒等映射) */
    lapic_base = (volatile uint32_t *)g_smp_info.lapic_base;

    /* 启用 LAPIC */
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE;
    svr = (svr & 0xFFFFFF00) | 0xFF; /* 伪中断向量 0xFF */
    lapic_write(LAPIC_SVR, svr);

    /* 清除 ESR */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    /* 屏蔽所有 LVT 条目 */
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERR, LVT_MASKED);
    if ((lapic_read(LAPIC_VER) >> 16) >= 4) {
        lapic_write(LAPIC_LVT_PERF, LVT_MASKED);
    }

    /* 设置任务优先级为 0 (接受所有中断) */
    lapic_write(LAPIC_TPR, 0);

    /* 发送 EOI 清除任何挂起的中断 */
    lapic_eoi();

    pr_ok("LAPIC: Initialized (ID=%u, ver=%u)",
          lapic_get_id(), lapic_read(LAPIC_VER) & 0xFF);
}

/**
 * 等待 IPI 发送完成
 */
static void lapic_wait_icr(void) {
    while (lapic_read(LAPIC_ICR_LO) & ICR_SEND_PENDING) {
        cpu_pause();
    }
}

void lapic_send_ipi(uint8_t lapic_id, uint8_t vector) {
    if (!lapic_base) {
        return;
    }

    lapic_wait_icr();

    /* 设置目标 LAPIC ID */
    lapic_write(LAPIC_ICR_HI, (uint32_t)lapic_id << 24);

    /* 发送 IPI: Fixed delivery, Edge triggered */
    lapic_write(LAPIC_ICR_LO, vector | ICR_FIXED | ICR_PHYSICAL | ICR_ASSERT | ICR_EDGE);

    lapic_wait_icr();
}

void lapic_send_ipi_all(uint8_t vector) {
    if (!lapic_base) {
        return;
    }

    lapic_wait_icr();

    /* 广播到除自己外的所有 CPU */
    lapic_write(LAPIC_ICR_LO, vector | ICR_FIXED | ICR_ALL_EXC_SELF | ICR_ASSERT | ICR_EDGE);

    lapic_wait_icr();
}

/**
 * 发送 INIT IPI
 */
void lapic_send_init(uint8_t lapic_id) {
    if (!lapic_base) {
        return;
    }

    lapic_wait_icr();

    lapic_write(LAPIC_ICR_HI, (uint32_t)lapic_id << 24);
    lapic_write(LAPIC_ICR_LO, ICR_INIT | ICR_PHYSICAL | ICR_ASSERT | ICR_LEVEL);

    lapic_wait_icr();
}

/**
 * 发送 INIT 解除 IPI
 */
void lapic_send_init_deassert(void) {
    if (!lapic_base) {
        return;
    }

    lapic_wait_icr();

    lapic_write(LAPIC_ICR_LO, ICR_INIT | ICR_ALL_INC_SELF | ICR_DEASSERT | ICR_LEVEL);

    lapic_wait_icr();
}

/**
 * 发送 STARTUP IPI
 *
 * @param lapic_id 目标 LAPIC ID
 * @param vector   启动代码页地址 (4KB 对齐, 低 20 位的高 8 位)
 */
void lapic_send_sipi(uint8_t lapic_id, uint8_t vector) {
    if (!lapic_base) {
        return;
    }

    lapic_wait_icr();

    lapic_write(LAPIC_ICR_HI, (uint32_t)lapic_id << 24);
    lapic_write(LAPIC_ICR_LO, vector | ICR_STARTUP | ICR_PHYSICAL | ICR_ASSERT | ICR_EDGE);

    lapic_wait_icr();
}

/* 用于校准 LAPIC 定时器的 tick 计数 */
static volatile uint32_t calibration_ticks = 0;

/**
 * PIT 校准回调 (临时使用 PIT 进行校准)
 */
void lapic_calibration_tick(void) {
    calibration_ticks++;
}

/**
 * 初始化 LAPIC 定时器
 *
 * @param freq 目标频率 (Hz)
 */
void lapic_timer_init(uint32_t freq) {
    if (!lapic_base) {
        return;
    }

    /*
     * 校准 LAPIC 定时器
     *
     * 使用 PIT Channel 2 进行校准:
     * 1. 设置 LAPIC 定时器为最大值
     * 2. 等待固定时间 (使用 PIT)
     * 3. 读取 LAPIC 定时器剩余值
     * 4. 计算 LAPIC 定时器频率
     *
     * 简化实现: 假设 LAPIC 定时器频率约为 1GHz / 16 = 62.5MHz
     * 对于 100Hz, 初始计数 = 62500000 / 100 = 625000
     */

    /* 设置分频为 16 */
    lapic_write(LAPIC_TIMER_DCR, TIMER_DIV_16);

    /*
     * 简单校准: 使用 PIT 进行 10ms 测量
     * PIT 频率: 1193182 Hz
     * 10ms = 11931 个 PIT tick
     */

    /* 设置 PIT Channel 0 为单次模式 */
    outb(0x43, 0x30); /* Channel 0, lobyte/hibyte, Mode 0 */
    uint16_t pit_count = 11932; /* ~10ms */
    outb(0x40, pit_count & 0xFF);
    outb(0x40, (pit_count >> 8) & 0xFF);

    /* 启动 LAPIC 定时器 (单次模式, 最大计数) */
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);

    /* 等待 PIT 倒计时完成 */
    uint8_t status;
    do {
        outb(0x43, 0xE2); /* 读回命令: Channel 0 */
        status = inb(0x40);
    } while (!(status & 0x80)); /* 等待 OUT 引脚变高 */

    /* 停止 LAPIC 定时器 */
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);

    /* 计算 LAPIC 定时器频率 */
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
    uint32_t lapic_freq = elapsed * 100; /* 10ms -> 1s */

    /* 计算目标频率需要的初始计数值 */
    uint32_t init_count = lapic_freq / freq;
    if (init_count == 0) {
        init_count = 1;
    }

    pr_debug("LAPIC Timer: freq=%u Hz, init_count=%u (target=%u Hz)",
             lapic_freq, init_count, freq);

    /* 配置 LAPIC 定时器: 周期模式, 向量 0x20 (与 PIT 相同) */
    lapic_write(LAPIC_LVT_TIMER, 0x20 | LVT_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_ICR, init_count);
}

void lapic_timer_stop(void) {
    if (!lapic_base) {
        return;
    }
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
}
