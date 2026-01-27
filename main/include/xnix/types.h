/**
 * @file types.h
 * @brief 内核类型定义
 *
 * 整合 ABI 类型和架构类型,提供内核使用的完整类型系统.
 */

#ifndef XNIX_TYPES_H
#define XNIX_TYPES_H

/* ABI 层:固定宽度整数 + 契约类型 */
#include <xnix/abi/stdint.h>
#include <xnix/abi/types.h>

/* 架构层:架构相关类型 */
#include <asm/types.h>

/*
 * 架构相关类型别名
 *
 * 这些类型的大小取决于架构(32 位 vs 64 位)
 */
typedef __arch_size_t    size_t;
typedef __arch_ssize_t   ssize_t;
typedef __arch_uintptr_t uintptr_t;
typedef __arch_intptr_t  intptr_t;
typedef __arch_ptrdiff_t ptrdiff_t;

/* 架构相关极值 */
#define SIZE_MAX    __ARCH_SIZE_MAX
#define PTRDIFF_MIN __ARCH_PTRDIFF_MIN
#define PTRDIFF_MAX __ARCH_PTRDIFF_MAX

/* 通用常量 */
#define NULL ((void *)0)

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

/**
 * 控制台颜色 (4-bit VGA/ANSI 兼容)
 *
 * kprintf 格式符映射:
 *   %K=黑 %R=红 %G=绿 %Y=黄 %B=蓝 %M=品红 %C=青 %W=白 %N=重置
 */
typedef enum {
    KCOLOR_BLACK   = 0,
    KCOLOR_BLUE    = 1,
    KCOLOR_GREEN   = 2,
    KCOLOR_CYAN    = 3,
    KCOLOR_RED     = 4,
    KCOLOR_MAGENTA = 5,
    KCOLOR_BROWN   = 6,
    KCOLOR_LGRAY   = 7,
    KCOLOR_DGRAY   = 8,
    KCOLOR_LBLUE   = 9,
    KCOLOR_LGREEN  = 10,
    KCOLOR_LCYAN   = 11,
    KCOLOR_LRED    = 12,
    KCOLOR_PINK    = 13,
    KCOLOR_YELLOW  = 14,
    KCOLOR_WHITE   = 15,
    KCOLOR_DEFAULT = -1,
} kcolor_t;

#endif /* XNIX_TYPES_H */
