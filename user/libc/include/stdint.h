/**
 * @file stdint.h
 * @brief 标准整数类型定义
 *
 * 基础类型来自 ABI 层，保证与内核一致。
 */

#ifndef _STDINT_H
#define _STDINT_H

/* 精确宽度整数类型（从 ABI 层导入） */
#include <xnix/abi/stdint.h>

/* 最小宽度整数类型 */
typedef int8_t  int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;

typedef uint8_t  uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

/* 快速整数类型 */
typedef int32_t int_fast8_t;
typedef int32_t int_fast16_t;
typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t;

typedef uint32_t uint_fast8_t;
typedef uint32_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

/* 指针相关整数类型（32 位架构） */
typedef int32_t  intptr_t;
typedef uint32_t uintptr_t;

/* 最大宽度整数类型 */
typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

/* 指针相关极值 */
#define INTPTR_MIN  INT32_MIN
#define INTPTR_MAX  INT32_MAX
#define UINTPTR_MAX UINT32_MAX

#define SIZE_MAX    UINT32_MAX
#define PTRDIFF_MIN INT32_MIN
#define PTRDIFF_MAX INT32_MAX

#endif /* _STDINT_H */
