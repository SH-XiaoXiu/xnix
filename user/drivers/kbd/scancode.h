/**
 * @file scancode.h
 * @brief 扫描码翻译
 */

#ifndef KBD_SCANCODE_H
#define KBD_SCANCODE_H

#include <stdint.h>

/* 方向键返回值 */
#define KEY_UP    (-2)
#define KEY_DOWN  (-3)
#define KEY_LEFT  (-4)
#define KEY_RIGHT (-5)

/**
 * 翻译扫描码为字符
 *
 * @param scancode 原始扫描码
 * @return 字符, -1=无输出, KEY_UP/DOWN/LEFT/RIGHT=方向键
 */
int scancode_to_char(uint8_t scancode);

#endif /* KBD_SCANCODE_H */
