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
 * @param str 日志内容
 */
void klog(const char *str);

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

#endif
