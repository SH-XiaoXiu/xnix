/**
 * @file debug.h
 * @brief 内核调试与错误处理接口
 * @author XiaoXiu
 * @date 2026-01-25
 */

#ifndef XNIX_DEBUG_H
#define XNIX_DEBUG_H

#include <xnix/types.h>

/**
 * @brief 系统崩溃函数
 * @param fmt 格式化字符串
 */
void panic(const char *fmt, ...);

/**
 * @brief 断言失败处理
 */
void __assert_fail(const char *expr, const char *file, int line, const char *func);

/* 断言宏 */
#define ASSERT(expr) \
    if (!(expr)) __assert_fail(#expr, __FILE__, __LINE__, __func__)

/* 致命错误检查 */
#define BUG_ON(condition) do { \
    if (condition) { \
        panic("BUG: %s at %s:%d", #condition, __FILE__, __LINE__); \
    } \
} while (0)

/* 警告检查 */
#define WARN_ON(condition) do { \
    if (condition) { \
        klog(LOG_WARN, "WARNING: %s at %s:%d\n", #condition, __FILE__, __LINE__); \
    } \
} while (0)

#endif /* XNIX_DEBUG_H */
