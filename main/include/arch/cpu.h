/**
 * @file arch/cpu.h
 * @brief CPU 控制接口(架构无关)
 * @author XiaoXiu
 */

#ifndef XNIX_ARCH_CPU_H
#define XNIX_ARCH_CPU_H

/* 包含架构特定实现 */
#include <asm/cpu.h>

struct thread; /* 前向声明 */

/* 架构初始化(由各架构实现)*/
void arch_early_init(void);
void arch_init(void);

/**
 * 架构特定的线程切换钩子
 * 在上下文切换前调用,用于处理架构相关的状态切换(如 VMM, TSS)
 */
void arch_thread_switch(struct thread *next);

#endif
