/**
 * @file stdio.h
 * @brief 内核标准输入输出接口
 * @author XiaoXiu
 * @date 2026-01-20
 */

#ifndef XNIX_STDIO_H
#define XNIX_STDIO_H

/**
 * @brief 输出单个字符
 * @param c 要输出的字符
 */
void kputc(char c);

/**
 * @brief 输出字符串
 * @param str 要输出的字符串
 */
void kputs(const char *str);

/**
 * @brief 内核日志输出
 * @param level 日志级别 (LOG_*)
 * @param fmt 格式化字符串
 */
void klog(int level, const char *fmt, ...);

/* 日志级别定义 */
#define LOG_NONE 0
#define LOG_ERR  1 /* 错误条件 */
#define LOG_WARN 2 /* 警告条件 */
#define LOG_INFO 3 /* 信息性消息 */
#define LOG_DBG  4 /* 调试级消息 */
#define LOG_OK   5 /* 成功消息 */

#include <xnix/config.h>

/* 便捷日志宏 */
#define pr_err(fmt, ...)  klog(LOG_ERR, fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...) klog(LOG_WARN, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) klog(LOG_INFO, fmt, ##__VA_ARGS__)
#define pr_ok(fmt, ...)   klog(LOG_OK, fmt, ##__VA_ARGS__)

#ifdef CFG_DEBUG
#define pr_debug(fmt, ...) klog(LOG_DBG, fmt, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {                   \
    } while (0)
/**
 * @brief 格式化输出到字符串
 * @param buf 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ... 可变参数
 * @return 写入的字符数
 */
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif

/**
 * @brief 格式化输出
 * @param fmt 格式字符串
 *
 * 支持的格式符:
 *   %s %c %d %i %u %x %p %%
 *   %K=黑 %R=红 %G=绿 %Y=黄 %B=蓝 %M=品红 %C=青 %W=白 %N=重置
 *
 * @param ... 可变参数
 */
void kprintf(const char *fmt, ...);

/**
 * @brief 核心格式化输出 (va_list版本)
 * @param fmt 格式化字符串
 * @param args 参数列表
 */
void vkprintf(const char *fmt, __builtin_va_list args);

#endif
