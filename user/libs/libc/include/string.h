/**
 * @file string.h
 * @brief 字符串操作函数
 */

#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

/* 内存操作 */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);

/* 字符串操作 */
size_t strlen(const char *s);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
char  *strchr(const char *s, int c);

#endif /* _STRING_H */
