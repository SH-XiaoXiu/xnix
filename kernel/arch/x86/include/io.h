/**
 * @file io.h
 * @brief x86 端口输入输出
 * @author XiaoXiu
 * @date 2026-01-20
 */

#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <arch/types.h>

/**
 * @brief 向端口写入一个字节
 * @param port 端口地址
 * @param val 要写入的值
 */
void arch_outb(uint16_t port, uint8_t val);

/**
 * @brief 从端口读取一个字节
 * @param port 端口地址
 * @return 读取的值
 */
uint8_t arch_inb(uint16_t port);

#endif
