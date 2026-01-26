/**
 * @file stddef.h
 * @brief 标准定义
 */

#ifndef _STDDEF_H
#define _STDDEF_H

#include <stdint.h>

/* NULL 指针常量 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* 类型定义 */
typedef uint32_t size_t;
typedef int32_t  ptrdiff_t;

/* offsetof 宏 */
#define offsetof(type, member) ((size_t)&((type *)0)->member)

#endif /* _STDDEF_H */
