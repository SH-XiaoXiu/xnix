/**
 * @file types.h
 * @brief 内核公共类型定义
 * @author XiaoXiu
 * @date 2026-01-22
 */

#ifndef XNIX_TYPES_H
#define XNIX_TYPES_H

/**
 * @brief 控制台颜色 (4-bit VGA/ANSI 兼容)
 *
 * kprintf 格式符映射:
 *   %K=黑 %R=红 %G=绿 %Y=黄 %B=蓝 %M=品红 %C=青 %W=白 %N=重置
 */
typedef enum {
    KCOLOR_BLACK   = 0,
    KCOLOR_BLUE    = 1,
    KCOLOR_GREEN   = 2,
    KCOLOR_CYAN    = 3,
    KCOLOR_RED     = 4,
    KCOLOR_MAGENTA = 5,
    KCOLOR_BROWN   = 6,
    KCOLOR_LGRAY   = 7,
    KCOLOR_DGRAY   = 8,
    KCOLOR_LBLUE   = 9,
    KCOLOR_LGREEN  = 10,
    KCOLOR_LCYAN   = 11,
    KCOLOR_LRED    = 12,
    KCOLOR_PINK    = 13,
    KCOLOR_YELLOW  = 14,
    KCOLOR_WHITE   = 15,
    KCOLOR_DEFAULT = -1,
} kcolor_t;

#endif
