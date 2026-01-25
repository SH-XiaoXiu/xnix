/**
 * @file arch/cpu.h
 * @brief CPU 控制接口(架构无关)
 * @author XiaoXiu
 */

#ifndef XNIX_ARCH_CPU_H
#define XNIX_ARCH_CPU_H

/* 包含架构特定实现 */
#include <asm/cpu.h>

/* 架构初始化(由各架构实现)*/
void arch_early_init(void);
void arch_init(void);

#endif
