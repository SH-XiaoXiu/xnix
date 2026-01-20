/**
 * @file serial.h
 * @brief 串口驱动接口（8250/16550 兼容）
 * @author XiaoXiu
 * @date 2026-01-20
 */

#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <xstd/stdint.h>

/* 标准串口端口地址 */
#define SERIAL_COM1  0x3F8
#define SERIAL_COM2  0x2F8
#define SERIAL_COM3  0x3E8
#define SERIAL_COM4  0x2E8

/**
 * @brief 初始化串口
 * @param port 端口基地址
 */
void serial_init(uint16_t port);

/**
 * @brief 输出单个字符
 * @param port 端口基地址
 * @param c 要输出的字符
 */
void serial_putc(uint16_t port, char c);

/**
 * @brief 输出字符串
 * @param port 端口基地址
 * @param str 要输出的字符串
 */
void serial_puts(uint16_t port, const char* str);

#endif
