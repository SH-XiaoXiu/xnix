/**
 * @file types.h
 * @brief x86 架构基础类型定义
 * @author XiaoXiu
 * @date 2026-01-20
 */

#ifndef ARCH_TYPES_H
#define ARCH_TYPES_H

/* 有符号整数 */
typedef signed char      int8_t;
typedef signed short     int16_t;
typedef signed int       int32_t;
typedef signed long long int64_t;

/* 无符号整数 */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

/* 指针相关（32位） */
typedef uint32_t uintptr_t;
typedef int32_t  intptr_t;
typedef uint32_t size_t;
typedef int32_t  ssize_t;
typedef int32_t  ptrdiff_t;

/* 极值 */
#define INT8_MIN    (-128)
#define INT8_MAX    (127)
#define UINT8_MAX   (255)
#define INT16_MIN   (-32768)
#define INT16_MAX   (32767)
#define UINT16_MAX  (65535)
#define INT32_MIN   (-2147483647 - 1)
#define INT32_MAX   (2147483647)
#define UINT32_MAX  (4294967295U)
#define INT64_MIN   (-9223372036854775807LL - 1)
#define INT64_MAX   (9223372036854775807LL)
#define UINT64_MAX  (18446744073709551615ULL)
#define SIZE_MAX    UINT32_MAX
#define PTRDIFF_MIN INT32_MIN
#define PTRDIFF_MAX INT32_MAX

/* 通用 */
#define NULL ((void *)0)

#ifndef __cplusplus
typedef _Bool bool;
#define true  1
#define false 0
#endif

#endif
