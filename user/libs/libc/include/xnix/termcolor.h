/**
 * @file termcolor.h
 * @brief 终端颜色控制接口(用户态)
 *
 * 颜色是终端渲染属性,通过 ttyd 转发到实际输出设备驱动.
 */

#ifndef XNIX_TERMCOLOR_H
#define XNIX_TERMCOLOR_H

#include <stdint.h>
#include <stdio.h>

/**
 * @brief 终端颜色(与 VGA 16 色编号一致)
 */
enum term_color {
    TERM_COLOR_BLACK         = 0,
    TERM_COLOR_BLUE          = 1,
    TERM_COLOR_GREEN         = 2,
    TERM_COLOR_CYAN          = 3,
    TERM_COLOR_RED           = 4,
    TERM_COLOR_MAGENTA       = 5,
    TERM_COLOR_BROWN         = 6,
    TERM_COLOR_LIGHT_GREY    = 7,
    TERM_COLOR_DARK_GREY     = 8,
    TERM_COLOR_LIGHT_BLUE    = 9,
    TERM_COLOR_LIGHT_GREEN   = 10,
    TERM_COLOR_LIGHT_CYAN    = 11,
    TERM_COLOR_LIGHT_RED     = 12,
    TERM_COLOR_LIGHT_MAGENTA = 13,
    TERM_COLOR_LIGHT_BROWN   = 14,
    TERM_COLOR_WHITE         = 15,
};

int termcolor_set(FILE *stream, enum term_color fg, enum term_color bg);
int termcolor_reset(FILE *stream);

#endif /* XNIX_TERMCOLOR_H */
