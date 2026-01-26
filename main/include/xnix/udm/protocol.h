/**
 * @file protocol.h
 * @brief UDM 协议基础定义
 */

#ifndef XNIX_UDM_PROTOCOL_H
#define XNIX_UDM_PROTOCOL_H

#include <stdint.h>

/* UDM 消息约定:msg.regs.data[0] 永远是 opcode */
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])

/* 通用返回码 */
#define UDM_OK          0
#define UDM_ERR_UNKNOWN (-1)
#define UDM_ERR_INVALID (-2)

#endif /* XNIX_UDM_PROTOCOL_H */
