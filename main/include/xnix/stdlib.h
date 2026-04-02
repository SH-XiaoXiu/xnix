/**
 * @file stdlib.h
 * @brief 标准库函数（内核用）
 */

#ifndef XNIX_STDLIB_H
#define XNIX_STDLIB_H

#include <xnix/types.h>

long          strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
int           atoi(const char *s);
long          atol(const char *s);

#endif /* XNIX_STDLIB_H */
