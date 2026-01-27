/**
 * @file smp.c
 * @brief x86 SMP 实现
 *
 * 阶段 1: 使用 LAPIC 实现 IPI 发送
 * 阶段 2: AP 启动
 */

#include <arch/smp.h>

#include <asm/apic.h>
#include <asm/smp_defs.h>

/* SMP 信息 (由 lapic.c 定义) */
extern struct smp_info g_smp_info;

/* 当前 CPU 在线状态 */
static volatile bool cpu_online[CFG_MAX_CPUS] = {true}; /* BSP 默认在线 */

cpu_id_t cpu_current_id(void) {
    if (!g_smp_info.apic_available) {
        return 0;
    }

    uint8_t lapic_id = lapic_get_id();

    /* 查找 LAPIC ID 对应的逻辑 CPU ID */
    for (uint32_t i = 0; i < g_smp_info.cpu_count; i++) {
        if (g_smp_info.lapic_ids[i] == lapic_id) {
            return i;
        }
    }
    return 0; /* fallback */
}

uint32_t cpu_count(void) {
#ifdef ENABLE_SMP
    return g_smp_info.cpu_count;
#else
    return 1;
#endif
}

bool cpu_is_online(cpu_id_t cpu) {
    if (cpu >= CFG_MAX_CPUS) {
        return false;
    }
    return cpu_online[cpu];
}

void cpu_set_online(cpu_id_t cpu, bool online) {
    if (cpu < CFG_MAX_CPUS) {
        cpu_online[cpu] = online;
    }
}

void smp_send_ipi(cpu_id_t cpu, uint8_t vector) {
    if (!g_smp_info.apic_available || cpu >= g_smp_info.cpu_count) {
        return;
    }

    uint8_t lapic_id = g_smp_info.lapic_ids[cpu];
    lapic_send_ipi(lapic_id, vector);
}

void smp_send_ipi_all(uint8_t vector) {
    if (!g_smp_info.apic_available) {
        return;
    }

    lapic_send_ipi_all(vector);
}
