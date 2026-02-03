/**
 * @file d/protocol/kbd.h
 * @brief 键盘协议定义
 */

#ifndef D_PROTOCOL_KBD_H
#define D_PROTOCOL_KBD_H

#include <stdint.h>

/* Helper macros for message parsing */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/* 键盘操作码 */
enum udm_kbd_op {
    UDM_KBD_GETCHAR = 1, /* 获取一个字符(阻塞) */
};

/* IRQ 编号 */
#define IRQ_KEYBOARD 1

#endif /* D_PROTOCOL_KBD_H */
