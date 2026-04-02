/**
 * @file scancode.h
 * @brief 扫描码翻译
 */

#ifndef KBD_SCANCODE_H
#define KBD_SCANCODE_H

#include <stdint.h>
#include <xnix/abi/input.h>

/* 方向键返回值 */
#define KEY_UP    (-2)
#define KEY_DOWN  (-3)
#define KEY_LEFT  (-4)
#define KEY_RIGHT (-5)

/**
 * 翻译扫描码为字符 (TTY 路径)
 *
 * @param scancode 原始扫描码
 * @return 字符, -1=无输出, KEY_UP/DOWN/LEFT/RIGHT=方向键
 */
int scancode_to_char(uint8_t scancode);

/**
 * 翻译扫描码为输入事件 (GUI 路径)
 *
 * 与 scancode_to_char 不同,此函数对每个扫描码(含 release)
 * 都会产生事件.两个函数共存互不干扰.
 *
 * @param scancode 原始扫描码
 * @param ev       输出事件
 * @return 0 事件产生, -1 内部状态(E0 前缀消耗)
 */
int scancode_to_event(uint8_t scancode, struct input_event *ev);

/**
 * 获取当前修饰键状态
 *
 * @return INPUT_MOD_* 位掩码
 */
uint8_t scancode_get_modifiers(void);

#endif /* KBD_SCANCODE_H */
