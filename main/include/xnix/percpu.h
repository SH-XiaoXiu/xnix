#ifndef XNIX_PERCPU_H
#define XNIX_PERCPU_H

#include <xnix/config.h>
#include <xnix/types.h>

/**
 * Per-CPU 变量抽象
 *
 * 内核子系统只使用 Per-CPU 抽象,不关心底层是单核还是多核.
 *
 * 编译裁切:
 * - CFG_MAX_CPUS == 1: 单核优化,Per-CPU 退化为全局变量
 * - CFG_MAX_CPUS > 1:  多核支持,Per-CPU 使用数组实现
 *
 * 启动裁切:
 * - 对于主流平台(x86/ARM),CFG_MAX_CPUS 通常配置为较大值(如 256)
 * - 启动时由 HAL 探测实际 CPU 数量,只使用 [0..N) 部分
 * - 对于嵌入式平台,可配置为固定值(如 1 或 4)
 *
 * 实现方式:
 * - 当前使用数组方式
 * - 未来可优化为段寄存器寻址(GS/FS)以提高性能
 */

#if CFG_MAX_CPUS == 1

/* 单核优化: 退化为普通全局变量 */

#define DEFINE_PER_CPU(type, name) type per_cpu__##name

#define DECLARE_PER_CPU(type, name) extern type per_cpu__##name

#define this_cpu_ptr(ptr) (&per_cpu__##ptr)

#define this_cpu_read(var) (per_cpu__##var)

#define this_cpu_write(var, val) (per_cpu__##var = (val))

#define per_cpu(var, cpu) (per_cpu__##var)

#define per_cpu_ptr(var, cpu) (&per_cpu__##var)

#else /* CFG_MAX_CPUS > 1 */

/* 多核支持: 使用数组实现 */

#define DEFINE_PER_CPU(type, name) \
    __attribute__((section(".data.percpu"))) type per_cpu__##name[CFG_MAX_CPUS]

#define DECLARE_PER_CPU(type, name) extern type per_cpu__##name[CFG_MAX_CPUS]

#define this_cpu_ptr(ptr) (&per_cpu__##ptr[cpu_current_id()])

#define this_cpu_read(var) (per_cpu__##var[cpu_current_id()])

#define this_cpu_write(var, val) (per_cpu__##var[cpu_current_id()] = (val))

#define per_cpu(var, cpu) (per_cpu__##var[(cpu)])

#define per_cpu_ptr(var, cpu) (&per_cpu__##var[(cpu)])

#endif /* CFG_MAX_CPUS */

/**
 * 获取当前 CPU ID
 * 由 arch 层实现,内核不应直接调用,仅供 Per-CPU 宏内部使用
 *
 * 单核时始终返回 0
 */
cpu_id_t cpu_current_id(void);

/**
 * 获取系统实际 CPU 数量
 * 内核子系统如需获取 CPU 数量,应使用此函数而不是直接依赖 arch/smp.h
 */
uint32_t percpu_cpu_count(void);

#endif /* XNIX_PERCPU_H */
