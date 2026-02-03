/**
 * @file d/protocol/serial.h
 * @brief 串口控制台协议定义
 */

#ifndef D_PROTOCOL_SERIAL_H
#define D_PROTOCOL_SERIAL_H

#include <stdint.h>

/* Helper macros for message parsing */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/* 控制台操作码 */
enum udm_console_op {
    UDM_CONSOLE_PUTC        = 1, /* data[1] = char */
    UDM_CONSOLE_SET_COLOR   = 2, /* data[1] = color */
    UDM_CONSOLE_RESET_COLOR = 3,
    UDM_CONSOLE_CLEAR       = 4,
    UDM_CONSOLE_WRITE       = 5, /* data[1..] = 字符串 */
};

/* WRITE 操作可用的最大字节数 */
#define UDM_CONSOLE_WRITE_MAX 28 /* 7*4 bytes from data[1..7] */

#endif /* D_PROTOCOL_SERIAL_H */
