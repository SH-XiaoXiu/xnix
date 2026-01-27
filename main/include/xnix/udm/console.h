/**
 * @file console.h
 * @brief UDM 控制台协议定义
 */

#ifndef XNIX_UDM_CONSOLE_H
#define XNIX_UDM_CONSOLE_H

#include <xnix/abi/ipc.h>
#include <xnix/udm/protocol.h>

/* 控制台操作码 */
enum udm_console_op {
    UDM_CONSOLE_PUTC        = 1, /* data[1] = char */
    UDM_CONSOLE_SET_COLOR   = 2, /* data[1] = color */
    UDM_CONSOLE_RESET_COLOR = 3,
    UDM_CONSOLE_CLEAR       = 4,
    UDM_CONSOLE_WRITE       = 5, /* data[1..] = 字符串 */
};

/* WRITE 操作可用的最大字节数,从 ABI 常量派生 */
#define UDM_CONSOLE_WRITE_MAX ABI_IPC_MSG_PAYLOAD_BYTES

#endif /* XNIX_UDM_CONSOLE_H */
