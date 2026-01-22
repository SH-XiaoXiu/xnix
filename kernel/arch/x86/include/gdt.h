/**
 * @file gdt.h
 * @brief 全局描述符表
 * @author XiaoXiu
 */

#ifndef ARCH_GDT_H
#define ARCH_GDT_H

#include <arch/types.h>

/**
 * @brief 初始化 GDT
 */
void gdt_init(void);

#endif
