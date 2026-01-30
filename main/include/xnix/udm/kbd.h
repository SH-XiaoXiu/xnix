/**
 * @file kbd.h
 * @brief UDM 键盘协议定义
 */

#ifndef XNIX_UDM_KBD_H
#define XNIX_UDM_KBD_H

#include <xnix/udm/protocol.h>

/* 键盘操作码 */
enum udm_kbd_op {
    UDM_KBD_GETCHAR = 1, /* 获取一个字符(阻塞) */
};

/* IRQ 编号 */
#define IRQ_KEYBOARD 1

#endif /* XNIX_UDM_KBD_H */
