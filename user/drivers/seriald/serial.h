/**
 * @file serial.h
 * @brief 串口驱动内部头文件
 */

#ifndef SERIALD_SERIAL_H
#define SERIALD_SERIAL_H

#include <stdint.h>

/**
 * 初始化串口
 *
 * @param io_cap I/O 端口能力句柄
 */
void serial_init(uint32_t io_cap);

/**
 * 输出单个字符
 */
void serial_putc(char c);

/**
 * 输出字符串
 */
void serial_puts(const char *s);

/**
 * 设置颜色（ANSI 转义序列）
 */
void serial_set_color(uint32_t color);

/**
 * 重置颜色
 */
void serial_reset_color(void);

/**
 * 清屏
 */
void serial_clear(void);

#endif /* SERIALD_SERIAL_H */
