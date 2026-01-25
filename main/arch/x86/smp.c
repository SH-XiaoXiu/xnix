/**
 * @file smp.c
 * @brief x86 SMP 实现
 *
 * 当前 单核实现
 * TODO: LAPIC 初始化,AP 核启动,IPI 发送
 */

#include <arch/smp.h>

/* 单核系统的桩实现 */

cpu_id_t cpu_current_id(void) {
    return 0;
}

uint32_t cpu_count(void) {
    return 1;
}

bool cpu_is_online(cpu_id_t cpu) {
    return cpu == 0;
}

void smp_send_ipi(cpu_id_t cpu, uint8_t vector) {
    (void)cpu;
    (void)vector;
    /* 单核无需 IPI */
}

void smp_send_ipi_all(uint8_t vector) {
    (void)vector;
    /* 单核无需 IPI */
}
