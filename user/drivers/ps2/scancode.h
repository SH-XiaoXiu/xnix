/**
 * @file scancode.h
 * @brief 扫描码翻译
 */

#ifndef PS2_SCANCODE_H
#define PS2_SCANCODE_H

#include <stdint.h>
#include <xnix/abi/input.h>

#define KEY_UP    (-2)
#define KEY_DOWN  (-3)
#define KEY_LEFT  (-4)
#define KEY_RIGHT (-5)

int scancode_to_char(uint8_t scancode);
int scancode_to_event(uint8_t scancode, struct input_event *ev);
uint8_t scancode_get_modifiers(void);

#endif
