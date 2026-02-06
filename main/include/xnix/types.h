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

/* 配置层:可调整的内核配置 */
#include <xnix/config.h>

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
#ifndef SIZE_MAX
#define SIZE_MAX __ARCH_SIZE_MAX
#endif

#ifndef PTRDIFF_MIN
#define PTRDIFF_MIN __ARCH_PTRDIFF_MIN
#endif

#ifndef PTRDIFF_MAX
#define PTRDIFF_MAX __ARCH_PTRDIFF_MAX
#endif

/* 通用常量 */
#define NULL ((void *)0)

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

/*
 * 内核内部可配置类型(编译裁切)
 *
 * 这些类型用于内核内部数据结构,大小可通过配置调整以节省内存.
 * 注意:ABI 类型(tid_t, pid_t)保持 32 位不变,以保证用户态兼容.
 */

/* 优先级类型 */
#if CFG_PRIORITY_BITS == 8
typedef int8_t priority_t;
#define PRIORITY_MIN INT8_MIN
#define PRIORITY_MAX INT8_MAX
#else
typedef int32_t priority_t;
#define PRIORITY_MIN INT32_MIN
#define PRIORITY_MAX INT32_MAX
#endif

/* 时间片类型 */
#if CFG_SLICE_BITS == 16
typedef uint16_t time_slice_t;
#define TIME_SLICE_MAX UINT16_MAX
#else
typedef uint32_t time_slice_t;
#define TIME_SLICE_MAX UINT32_MAX
#endif

#endif /* XNIX_TYPES_H */
