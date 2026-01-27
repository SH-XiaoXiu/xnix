/**
 * @file asm/types.h
 * @brief x86 (32-bit) 架构类型定义
 *
 * 只包含架构相关的类型,固定宽度整数在 abi/stdint.h
 */

#ifndef ASM_TYPES_H
#define ASM_TYPES_H

#include <xnix/abi/stdint.h>

#define __ARCH_BITS 32

/*
 * 架构相关类型
 *
 * 这些类型的大小取决于 CPU 架构(32 位 vs 64 位)
 */
typedef uint32_t __arch_size_t;
typedef int32_t  __arch_ssize_t;
typedef uint32_t __arch_uintptr_t;
typedef int32_t  __arch_intptr_t;
typedef int32_t  __arch_ptrdiff_t;

/* 架构相关极值 */
#define __ARCH_SIZE_MAX    UINT32_MAX
#define __ARCH_PTRDIFF_MIN INT32_MIN
#define __ARCH_PTRDIFF_MAX INT32_MAX

#endif /* ASM_TYPES_H */
