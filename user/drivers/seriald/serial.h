/**
 * @file serial.h
 * @brief 串口驱动内部头文件 (多端口)
 */

#ifndef SERIALD_SERIAL_H
#define SERIALD_SERIAL_H

#include <stdint.h>

/* 标准 COM 端口基地址 */
#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8
#define COM3_BASE 0x3E8
#define COM4_BASE 0x2E8

/* COM 端口 IRQ */
#define COM1_IRQ 4
#define COM2_IRQ 3
#define COM3_IRQ 4 /* 共享 */
#define COM4_IRQ 3 /* 共享 */

#define MAX_COM_PORTS 4

/**
 * 探测端口是否存在
 * @return 1 存在, 0 不存在
 */
int serial_probe(uint16_t port);

/**
 * 初始化串口硬件
 */
void serial_hw_init(uint16_t port);

/**
 * 开启串口接收中断
 */
void serial_enable_irq(uint16_t port);

/**
 * 输出单个字符
 */
void serial_putc_port(uint16_t port, char c);

/**
 * 输出字符串
 */
void serial_puts_port(uint16_t port, const char *s);

/**
 * 清屏 (发送 ANSI 序列)
 */
void serial_clear_port(uint16_t port);

/**
 * 检查是否有数据可读
 */
int serial_data_available(uint16_t port);

/**
 * 读取一个字符 (非阻塞)
 * @return 字符值, -1 无数据
 */
int serial_getc_port(uint16_t port);

#endif /* SERIALD_SERIAL_H */
