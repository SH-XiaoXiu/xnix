/**
 * @file xnix/protocol/input.h
 * @brief 统一输入事件协议
 *
 * 定义输入事件 IPC 操作码和寄存器编码格式.
 * kbd 和 moused 在各自端点上同时支持此协议和原有协议.
 * 操作码从 0x100 开始,不与现有协议冲突.
 */

#ifndef XNIX_PROTOCOL_INPUT_H
#define XNIX_PROTOCOL_INPUT_H

#include <xnix/abi/input.h>

/* 消息解析辅助宏 */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/**
 * 输入事件操作码 (0x100+ 避免与现有协议冲突)
 */
enum input_op {
    INPUT_OP_READ_EVENT = 0x100, /* 读取一个事件(阻塞) */
    INPUT_OP_POLL       = 0x101, /* 非阻塞检查是否有事件 */
};

/**
 * 事件编码到 IPC 寄存器
 *
 * 回复布局:
 *   data[0] = 0 (成功)
 *   data[1] = type | (modifiers << 8) | (code << 16)
 *   data[2] = (uint16_t)value | ((uint16_t)value2 << 16)
 *   data[3] = timestamp
 */
#define INPUT_PACK_REG1(ev) \
    ((uint32_t)(ev)->type | ((uint32_t)(ev)->modifiers << 8) | \
     ((uint32_t)(ev)->code << 16))

#define INPUT_PACK_REG2(ev) \
    ((uint32_t)(uint16_t)(ev)->value | \
     ((uint32_t)(uint16_t)(ev)->value2 << 16))

#define INPUT_PACK_REG3(ev) ((ev)->timestamp)

/* 解码宏 */
#define INPUT_UNPACK_TYPE(reg1)      ((uint8_t)((reg1) & 0xFF))
#define INPUT_UNPACK_MODIFIERS(reg1) ((uint8_t)(((reg1) >> 8) & 0xFF))
#define INPUT_UNPACK_CODE(reg1)      ((uint16_t)(((reg1) >> 16) & 0xFFFF))
#define INPUT_UNPACK_VALUE(reg2)     ((int16_t)((reg2) & 0xFFFF))
#define INPUT_UNPACK_VALUE2(reg2)    ((int16_t)(((reg2) >> 16) & 0xFFFF))
#define INPUT_UNPACK_TIMESTAMP(reg3) (reg3)

#endif /* XNIX_PROTOCOL_INPUT_H */
