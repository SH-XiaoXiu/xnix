#ifndef XNIX_PERCPU_H
#define XNIX_PERCPU_H

#include <xnix/config.h>
#include <xnix/types.h>

/*
 * Per-CPU 变量定义宏
 *
 * 在单核 (UP) 模式下,Per-CPU 变量退化为普通的全局变量.
 * 在多核 (SMP) 模式下,它们通常存储在特殊的段 (如 GS/FS) 或数组中.
 */

#ifdef ENABLE_SMP

/* SMP 实现: 使用数组或特定段 */
/* 目前使用数组方式*/
/* TODO: 优化为段寄存器寻址以提高性能 */

#define DEFINE_PER_CPU(type, name) \
    __attribute__((section(".data.percpu"))) type per_cpu__##name[CFG_MAX_CPUS]

#define DECLARE_PER_CPU(type, name) extern type per_cpu__##name[CFG_MAX_CPUS]

#define this_cpu_ptr(ptr) (&per_cpu__##ptr[cpu_current_id()])

#define this_cpu_read(var) (per_cpu__##var[cpu_current_id()])

#define this_cpu_write(var, val) (per_cpu__##var[cpu_current_id()] = (val))

#define per_cpu(var, cpu) (per_cpu__##var[(cpu)])

#else /* !ENABLE_SMP */

/* UP 实现: 普通全局变量 */

#define DEFINE_PER_CPU(type, name) type per_cpu__##name

#define DECLARE_PER_CPU(type, name) extern type per_cpu__##name

#define this_cpu_ptr(ptr) (&per_cpu__##ptr)

#define this_cpu_read(var) (per_cpu__##var)

#define this_cpu_write(var, val) (per_cpu__##var = (val))

#define per_cpu(var, cpu) (per_cpu__##var)

#endif /* ENABLE_SMP */

/* 获取当前 CPU ID (由 arch 实现) */
cpu_id_t cpu_current_id(void);

#endif /* XNIX_PERCPU_H */
