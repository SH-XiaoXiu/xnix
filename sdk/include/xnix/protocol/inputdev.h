/**
 * @file xnix/protocol/inputdev.h
 * @brief 输入设备 IPC 协议
 *
 * 定义输入设备 (PS/2 键盘, PS/2 鼠标等) 的标准通信协议.
 * 驱动实现此协议, 消费者 (termd) 通过此协议获取输入事件.
 *
 * 与 protocol/input.h (统一输入事件格式) 的关系:
 *   - input.h 定义事件的数据格式 (type, code, value 的编码方式)
 *   - inputdev.h 定义设备级 IPC 协议 (如何请求/获取事件)
 *
 * Opcode 范围: 0x300-0x3FF
 */

#ifndef XNIX_PROTOCOL_INPUTDEV_H
#define XNIX_PROTOCOL_INPUTDEV_H

#include <stdint.h>

/* ============== 操作码 ============== */

/**
 * INPUTDEV_READ - 阻塞读取一个输入事件
 *
 * Request:
 *   data[0] = INPUTDEV_READ
 *
 * Reply:
 *   data[0] = 0 成功, 负错误码失败
 *   data[1] = type | (modifiers << 8) | (code << 16)
 *   data[2] = (uint16_t)value | ((uint16_t)value2 << 16)
 *   data[3] = timestamp
 *
 * 事件编码与 protocol/input.h 一致 (INPUT_PACK_REG* / INPUT_UNPACK_*).
 */
#define INPUTDEV_READ 0x300

/**
 * INPUTDEV_POLL - 非阻塞检查是否有待处理事件
 *
 * Request:
 *   data[0] = INPUTDEV_POLL
 *
 * Reply:
 *   data[0] = 待处理事件数 (0 = 无事件)
 */
#define INPUTDEV_POLL 0x301

/**
 * INPUTDEV_INFO - 查询设备信息
 *
 * Request:
 *   data[0] = INPUTDEV_INFO
 *
 * Reply:
 *   data[0] = 0 成功
 *   data[1] = caps (INPUTDEV_CAP_*)
 *   data[2] = input_type (INPUTDEV_TYPE_*)
 *   data[3] = 设备实例号
 *   buffer  = 设备名 (null-terminated)
 */
#define INPUTDEV_INFO 0x302

/* ============== 能力标志 ============== */

#define INPUTDEV_CAP_KEY    (1 << 0) /* 设备产生按键事件 */
#define INPUTDEV_CAP_MOUSE  (1 << 1) /* 设备产生鼠标事件 */

/* ============== 设备类型 ============== */

#define INPUTDEV_TYPE_KEYBOARD 0
#define INPUTDEV_TYPE_MOUSE    1

#endif /* XNIX_PROTOCOL_INPUTDEV_H */
