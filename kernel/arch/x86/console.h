/**
 * @file console.h
 * @brief x86 控制台接口
 * @author XiaoXiu
 * @date 2026-01-20
 */

#ifndef ARCH_CONSOLE_H
#define ARCH_CONSOLE_H

/**
 * @brief 初始化控制台
 */
void arch_console_init(void);

/**
 * @brief 输出单个字符
 * @param c 要输出的字符
 */
void arch_putc(char c);

#endif
