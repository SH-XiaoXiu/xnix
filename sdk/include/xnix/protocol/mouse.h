/**
 * @file xnix/protocol/mouse.h
 * @brief 鼠标协议定义
 */

#ifndef XNIX_PROTOCOL_MOUSE_H
#define XNIX_PROTOCOL_MOUSE_H

#include <stdint.h>
#include <xnix/protocol/udm_errno.h>

/* 消息解析辅助宏 */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/* 鼠标操作码 */
enum udm_mouse_op {
    UDM_MOUSE_READ_PACKET = 1, /* 读取鼠标包(阻塞) */
    UDM_MOUSE_POLL        = 2, /* 非阻塞检查是否有数据 */
};

/**
 * 鼠标数据包
 *
 * READ_PACKET 回复:
 *   data[0] = UDM_OK
 *   data[1] = dx (int16_t, 低16位)
 *   data[2] = dy (int16_t, 低16位)
 *   data[3] = buttons (MOUSE_BTN_* 位掩码)
 */

/* 鼠标按键位掩码 */
#define MOUSE_BTN_LEFT   (1 << 0)
#define MOUSE_BTN_RIGHT  (1 << 1)
#define MOUSE_BTN_MIDDLE (1 << 2)

/* IRQ 编号 */
#define IRQ_MOUSE 12

#endif /* XNIX_PROTOCOL_MOUSE_H */
