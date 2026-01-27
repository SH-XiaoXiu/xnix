/**
 * @file abi/stdint.h
 * @brief 固定宽度整数类型 (ABI 基础)
 *
 * 自包含,不依赖任何其他头文件.
 * 这些类型在所有架构上大小相同.
 */

#ifndef XNIX_ABI_STDINT_H
#define XNIX_ABI_STDINT_H

/* 有符号整数 */
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

/* 无符号整数 */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

/* 极值 */
#define INT8_MIN   (-128)
#define INT8_MAX   (127)
#define UINT8_MAX  (255U)
#define INT16_MIN  (-32768)
#define INT16_MAX  (32767)
#define UINT16_MAX (65535U)
#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  (2147483647)
#define UINT32_MAX (4294967295U)
#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  (9223372036854775807LL)
#define UINT64_MAX (18446744073709551615ULL)

#endif /* XNIX_ABI_STDINT_H */
