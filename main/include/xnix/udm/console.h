/**
 * @file console.h
 * @brief UDM 控制台协议定义
 */

#ifndef XNIX_UDM_CONSOLE_H
#define XNIX_UDM_CONSOLE_H

#include <xnix/udm/protocol.h>

/* 控制台操作码 */
enum udm_console_op {
    UDM_CONSOLE_PUTC        = 1, /* data[1] = char */
    UDM_CONSOLE_SET_COLOR   = 2, /* data[1] = color */
    UDM_CONSOLE_RESET_COLOR = 3,
    UDM_CONSOLE_CLEAR       = 4,
};

#endif /* XNIX_UDM_CONSOLE_H */
