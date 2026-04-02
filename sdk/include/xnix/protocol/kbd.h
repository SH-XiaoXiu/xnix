/**
 * @file xnix/protocol/kbd.h
 * @brief 键盘协议定义
 */

#ifndef XNIX_PROTOCOL_KBD_H
#define XNIX_PROTOCOL_KBD_H

#include <stdint.h>
#include <xnix/protocol/udm_errno.h>

/* 消息解析辅助宏 */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])       /* 获取消息操作码 */
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1]) /* 获取消息第n个参数 */
#endif

/* 键盘操作码 */
enum udm_kbd_op {
    UDM_KBD_GETCHAR = 1, /* 获取一个字符(阻塞) */
};

/* IRQ 编号 */
#define IRQ_KEYBOARD 1

#endif /* XNIX_PROTOCOL_KBD_H */
