/**
 * @file lapic.c
 * @brief x86 Local APIC 驱动
 *
 * LAPIC 提供:
 * - 本地定时器中断
 * - 核间中断 (IPI) 发送
 * - 中断优先级管理
 */

#include <arch/cpu.h>
#include <arch/smp.h>

#include <asm/apic.h>
#include <asm/smp_defs.h>
#include <xnix/stdio.h>
#include <xnix/vmm.h>

/* LAPIC 映射的虚拟地址 (使用恒等映射) */
static volatile uint32_t *lapic_base = NULL;

/* MSR 读写 */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" ::"c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

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

    /*
     * 检查并强制切换到 xAPIC 模式
     * 如果 x2APIC 启用,MMIO 访问不工作,必须切换回 xAPIC
     */
    uint64_t apic_base_msr = rdmsr(MSR_IA32_APIC_BASE);
    if (apic_base_msr & APIC_BASE_X2APIC) {
        /* 先禁用 APIC,再重新启用为 xAPIC 模式 */
        wrmsr(MSR_IA32_APIC_BASE, apic_base_msr & ~(APIC_BASE_ENABLE | APIC_BASE_X2APIC));
        wrmsr(MSR_IA32_APIC_BASE, (apic_base_msr & ~APIC_BASE_X2APIC) | APIC_BASE_ENABLE);
    } else if (!(apic_base_msr & APIC_BASE_ENABLE)) {
        wrmsr(MSR_IA32_APIC_BASE, apic_base_msr | APIC_BASE_ENABLE);
    }

    paddr_t lapic_phys = LAPIC_BASE_DEFAULT;
    bool    mapped     = false;
    if (!lapic_base) {
        if (vmm_map_page(NULL, lapic_phys, lapic_phys,
                         VMM_PROT_READ | VMM_PROT_WRITE | VMM_PROT_NOCACHE) < 0) {
            pr_err("LAPIC: Failed to map LAPIC at 0x%x", lapic_phys);
            return;
        }
        lapic_base = (volatile uint32_t *)lapic_phys;
        mapped     = true;
    }

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
    lapic_write(LAPIC_LVT_LINT0, 0x700);
    lapic_write(LAPIC_LVT_LINT1, LVT_MASKED);
    lapic_write(LAPIC_LVT_ERR, LVT_MASKED);
    if ((lapic_read(LAPIC_VER) >> 16) >= 4) {
        lapic_write(LAPIC_LVT_PERF, LVT_MASKED);
    }

    /* 设置任务优先级为 0 (接受所有中断) */
    lapic_write(LAPIC_TPR, 0);

    /* 发送 EOI 清除任何挂起的中断 */
    lapic_eoi();

    if (mapped) {
        pr_ok("LAPIC: Initialized (ID=%u, ver=%u)", lapic_get_id(), lapic_read(LAPIC_VER) & 0xFF);
    }
}

/**
 * 等待 IPI 发送完成
 */
static void lapic_wait_icr(void) {
    uint32_t spins = 0;
    while (lapic_read(LAPIC_ICR_LO) & ICR_SEND_PENDING) {
        cpu_pause();
        spins++;
        if (spins == 10000000) {
            pr_err("LAPIC: ICR send pending stuck (cpu=%u)", cpu_current_id());
            break;
        }
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

/* BSP 校准后保存的定时器频率 (分频后,AP 复用) */
static volatile uint32_t lapic_timer_freq = 0;

/*
 * 虚拟化环境下的默认 LAPIC 定时器频率 (分频后)
 * QEMU/KVM 通常使用 1GHz 总线频率,分频 16 后约 62.5MHz
 */
#define LAPIC_TIMER_DEFAULT_FREQ 62500000

/* PIT 校准函数声明 */
extern void pit_calibration_start(uint16_t count);
extern bool pit_calibration_done(void);

/**
 * 使用 PIT Channel 2 校准 LAPIC Timer 频率
 *
 * PIT 频率固定为 1193182 Hz,用作精确时间参考.
 * 校准周期约 10ms (1/100 秒),所以 LAPIC 频率 ≈ elapsed * 100.
 *
 * @return LAPIC Timer 频率 (分频后), 失败返回 0
 */
static uint32_t lapic_calibrate_with_pit(void) {
    /*
     * 使用约 10ms 的校准周期: 1193182 / 100 ≈ 11932 ticks
     * 精确计算: 11932 / 1193182 = 0.010001... 秒
     * 误差约 0.01%,可忽略
     */
    const uint16_t pit_count = 11932;

    /* 启动 PIT Channel 2 计时 */
    pit_calibration_start(pit_count);

    /* 启动 LAPIC Timer 全量倒计数 */
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
    lapic_write(LAPIC_TIMER_DCR, TIMER_DIV_16);
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);

    /* 等待 PIT 计数完成 */
    while (!pit_calibration_done()) {
        cpu_pause();
    }

    /* 读取 LAPIC Timer 已消耗的计数值 */
    uint32_t lapic_elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);

    /* 停止 LAPIC Timer */
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);

    /*
     * 计算 LAPIC Timer 频率
     * 校准周期为 10ms (1/100 秒),所以频率 = elapsed * 100
     */
    uint32_t lapic_freq = lapic_elapsed * 100;

    return lapic_freq;
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

    uint32_t timer_freq;

    /* 检查是否已有校准值 (AP 复用 BSP 的校准结果) */
    if (lapic_timer_freq != 0) {
        timer_freq = lapic_timer_freq;
    } else {
        /* BSP 首次校准:使用 PIT Channel 2 进行精确测量 */
        timer_freq = lapic_calibrate_with_pit();

        /*
         * 合理性检查:如果校准结果不合理,使用默认值
         * 正常范围: 1MHz ~ 1GHz
         */
        if (timer_freq < 1000000 || timer_freq > 1000000000) {
            pr_warn("LAPIC Timer: calibration failed (%u Hz), using default", timer_freq);
            timer_freq = LAPIC_TIMER_DEFAULT_FREQ;
        }

        /* 保存校准结果供 AP 使用 */
        lapic_timer_freq = timer_freq;
        pr_ok("LAPIC Timer: %u Hz (calibrated)", timer_freq);
    }

    /* 计算目标频率需要的初始计数值 */
    uint32_t init_count = timer_freq / freq;

    /* 设置分频为 16 */
    lapic_write(LAPIC_TIMER_DCR, TIMER_DIV_16);

    /* 配置 LAPIC 定时器: 周期模式, 向量 0x20 */
    lapic_write(LAPIC_LVT_TIMER, 0x20 | LVT_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_ICR, init_count);
}

void lapic_timer_stop(void) {
    if (!lapic_base) {
        return;
    }
    lapic_write(LAPIC_LVT_TIMER, LVT_MASKED);
}
