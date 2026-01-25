/**
 * @file smp.h
 * @brief 对称多处理器 (SMP) 抽象接口
 *
 * 多核系统需要的基本操作
 */

#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#include <xnix/types.h>

typedef uint32_t cpu_id_t;

#define CPU_ID_INVALID ((cpu_id_t) - 1)

/**
 * 获取当前 CPU ID
 * 单核系统始终返回 0
 */
cpu_id_t cpu_current_id(void);

/**
 * 获取系统 CPU 总数
 * 单核系统返回 1
 */
uint32_t cpu_count(void);

/**
 * 检查某个 CPU 是否在线
 */
bool cpu_is_online(cpu_id_t cpu);

/**
 * 发送核间中断 (IPI - Inter-Processor Interrupt)
 * 用于通知其他核执行调度,刷新 TLB 等
 *
 * @param cpu 目标 CPU ID
 * @param vector 中断向量号
 */
void smp_send_ipi(cpu_id_t cpu, uint8_t vector);

/**
 * 广播 IPI 到所有其他 CPU
 */
void smp_send_ipi_all(uint8_t vector);

#endif
