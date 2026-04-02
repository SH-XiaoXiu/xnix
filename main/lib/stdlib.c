/**
 * @file stdlib.c
 * @brief 标准库函数（内核用）
 */

#include <xnix/stdlib.h>

static int is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int char_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 36; /* 无效 */
}

long strtol(const char *s, char **endptr, int base) {
    while (is_space(*s)) s++;

    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    long result = 0;
    const char *start = s;
    while (char_val(*s) < base) {
        result = result * base + char_val(*s);
        s++;
    }

    if (endptr) *endptr = (s == start) ? (char *)start : (char *)s;
    return neg ? -result : result;
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    while (is_space(*s)) s++;
    if (*s == '+') s++;

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    unsigned long result = 0;
    const char *start = s;
    while (char_val(*s) < (unsigned)base) {
        result = result * (unsigned)base + (unsigned)char_val(*s);
        s++;
    }

    if (endptr) *endptr = (s == start) ? (char *)start : (char *)s;
    return result;
}

int atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}

long atol(const char *s) {
    return strtol(s, NULL, 10);
}
