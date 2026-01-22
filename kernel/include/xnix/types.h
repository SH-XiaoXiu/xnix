/**
 * @file types.h
 * @brief 内核公共类型定义
 * @author XiaoXiu
 * @date 2026-01-22
 */

#ifndef XNIX_TYPES_H
#define XNIX_TYPES_H

#include <asm/types.h>

/* 有符号整数 (固定宽度) */
typedef __s8  int8_t;
typedef __s16 int16_t;
typedef __s32 int32_t;
typedef __s64 int64_t;

/* 无符号整数 (固定宽度) */
typedef __u8  uint8_t;
typedef __u16 uint16_t;
typedef __u32 uint32_t;
typedef __u64 uint64_t;

/* 架构相关类型 */
typedef __arch_size_t    size_t;
typedef __arch_ssize_t   ssize_t;
typedef __arch_uintptr_t uintptr_t;
typedef __arch_intptr_t  intptr_t;
typedef __arch_ptrdiff_t ptrdiff_t;

/* 固定宽度极值 */
#define INT8_MIN   (-128)
#define INT8_MAX   (127)
#define UINT8_MAX  (255)
#define INT16_MIN  (-32768)
#define INT16_MAX  (32767)
#define UINT16_MAX (65535)
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  (2147483647)
#define UINT32_MAX (4294967295U)
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  (9223372036854775807LL)
#define UINT64_MAX (18446744073709551615ULL)

/* 架构相关极值 */
#define SIZE_MAX    __ARCH_SIZE_MAX
#define PTRDIFF_MIN __ARCH_PTRDIFF_MIN
#define PTRDIFF_MAX __ARCH_PTRDIFF_MAX

/* 通用 */
#define NULL ((void *)0)

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

/**
 * @brief 控制台颜色 (4-bit VGA/ANSI 兼容)
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

#endif
