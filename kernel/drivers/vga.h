/**
 * @file vga.h
 * @brief VGA 文本模式驱动接口
 * @author XiaoXiu
 * @date 2026-01-20
 */

#ifndef DRIVERS_VGA_H
#define DRIVERS_VGA_H

#include <xstd/stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25

enum vga_color {
    VGA_BLACK        = 0,
    VGA_BLUE         = 1,
    VGA_GREEN        = 2,
    VGA_CYAN         = 3,
    VGA_RED          = 4,
    VGA_MAGENTA      = 5,
    VGA_BROWN        = 6,
    VGA_LIGHT_GREY   = 7,
    VGA_DARK_GREY    = 8,
    VGA_LIGHT_BLUE   = 9,
    VGA_LIGHT_GREEN  = 10,
    VGA_LIGHT_CYAN   = 11,
    VGA_LIGHT_RED    = 12,
    VGA_LIGHT_MAGENTA= 13,
    VGA_YELLOW       = 14,
    VGA_WHITE        = 15,
};

/**
 * @brief 初始化 VGA 文本模式
 * @param buffer 显存缓冲区地址
 */
void vga_init(void* buffer);

/**
 * @brief 设置前景和背景颜色
 * @param fg 前景色
 * @param bg 背景色
 */
void vga_set_color(enum vga_color fg, enum vga_color bg);

/**
 * @brief 清屏
 */
void vga_clear(void);

/**
 * @brief 输出单个字符（支持滚动）
 * @param c 要输出的字符
 */
void vga_putc(char c);

/**
 * @brief 在指定位置输出字符串
 * @param str 要输出的字符串
 * @param x 列坐标
 * @param y 行坐标
 */
void vga_puts_at(const char* str, int x, int y);

#endif
