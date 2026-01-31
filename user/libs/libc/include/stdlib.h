/**
 * @file stdlib.h
 * @brief 标准库函数
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

/**
 * 分配内存
 *
 * @param size 请求的字节数
 * @return 分配的内存指针,失败返回 NULL
 */
void *malloc(size_t size);

/**
 * 分配并清零内存
 *
 * @param nmemb 元素数量
 * @param size 每个元素的大小
 * @return 分配的内存指针,失败返回 NULL
 */
void *calloc(size_t nmemb, size_t size);

/**
 * 重新分配内存
 *
 * @param ptr 原内存指针(NULL 等同于 malloc)
 * @param size 新大小(0 等同于 free)
 * @return 新内存指针,失败返回 NULL(原内存不变)
 */
void *realloc(void *ptr, size_t size);

/**
 * 释放内存
 *
 * @param ptr 要释放的内存指针(NULL 安全)
 */
void free(void *ptr);

#endif /* _STDLIB_H */
