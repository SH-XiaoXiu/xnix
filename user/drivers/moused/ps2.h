/**
 * @file ps2.h
 * @brief PS/2 鼠标硬件协议
 */

#ifndef MOUSED_PS2_H
#define MOUSED_PS2_H

#include <stdint.h>

/* PS/2 端口 */
#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

/**
 * 初始化 PS/2 鼠标
 *
 * 启用第二端口,重置鼠标,启用数据报告.
 *
 * @return 0 成功, <0 失败
 */
int ps2_mouse_init(void);

/**
 * 解析 PS/2 3 字节鼠标数据包
 *
 * @param raw  原始 3 字节数据
 * @param dx   输出: X 位移
 * @param dy   输出: Y 位移
 * @param btns 输出: 按键状态 (MOUSE_BTN_* 位掩码)
 * @return 0 成功, -1 数据包无效
 */
int ps2_mouse_parse(const uint8_t raw[3], int16_t *dx, int16_t *dy,
                    uint8_t *btns);

#endif /* MOUSED_PS2_H */
