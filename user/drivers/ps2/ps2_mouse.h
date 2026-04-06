/**
 * @file ps2_mouse.h
 * @brief PS/2 鼠标硬件协议
 */

#ifndef PS2_PS2_MOUSE_H
#define PS2_PS2_MOUSE_H

#include <stdint.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

int ps2_mouse_init(void);
int ps2_mouse_parse(const uint8_t raw[3], int16_t *dx, int16_t *dy, uint8_t *btns);

#endif
