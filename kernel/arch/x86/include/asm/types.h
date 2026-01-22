/**
 * @file asm/types.h
 * @brief x86 (32-bit) 架构类型定义
 * @author XiaoXiu
 */

#ifndef ASM_TYPES_H
#define ASM_TYPES_H

#define __ARCH_BITS 32

/* 有符号整数 */
typedef signed char      __s8;
typedef signed short     __s16;
typedef signed int       __s32;
typedef signed long long __s64;

/* 无符号整数 */
typedef unsigned char      __u8;
typedef unsigned short     __u16;
typedef unsigned int       __u32;
typedef unsigned long long __u64;

/* 架构相关类型 (32位) */
typedef __u32 __arch_size_t;
typedef __s32 __arch_ssize_t;
typedef __u32 __arch_uintptr_t;
typedef __s32 __arch_intptr_t;
typedef __s32 __arch_ptrdiff_t;

/* 极值 (架构相关) */
#define __ARCH_SIZE_MAX    0xFFFFFFFFU
#define __ARCH_PTRDIFF_MIN (-2147483647 - 1)
#define __ARCH_PTRDIFF_MAX 2147483647

#endif
