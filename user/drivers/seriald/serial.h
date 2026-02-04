/**
 * @file serial.h
 * @brief 串口驱动内部头文件
 */

#ifndef SERIALD_SERIAL_H
#define SERIALD_SERIAL_H

#include <stdint.h>

/**
 * 初始化串口
 */
void serial_init(void);

/**
 * 开启串口中断
 */
void serial_enable_irq(void);

/**
 * 输出单个字符
 */
void serial_putc(char c);

/**
 * 输出字符串
 */
void serial_puts(const char *s);

/**
 * 清屏
 */
void serial_clear(void);

/**
 * 检查是否有数据可读
 * @return 非0表示有数据
 */
int serial_data_available(void);

/**
 * 读取一个字符(非阻塞)
 * @return 字符值,-1 表示无数据
 */
int serial_getc(void);

#endif /* SERIALD_SERIAL_H */
