/**
 * @file xnix/protocol/console.h
 * @brief Console/session manager 协议
 *
 * 当前阶段已显式引入 session 概念，但行为仍只切换前台 TTY。
 * 后续 GUI 接回时，在此协议上继续扩展 ownership 与输入路由。
 */

#ifndef XNIX_PROTOCOL_CONSOLE_H
#define XNIX_PROTOCOL_CONSOLE_H

#include <stdint.h>

enum console_op {
    CONSOLE_OP_SET_ACTIVE_TTY = 0x700, /* data[1] = tty_id */
    CONSOLE_OP_GET_ACTIVE_TTY = 0x701, /* reply data[0] = tty_id */
    CONSOLE_OP_GET_ACTIVE_SESSION = 0x702, /* reply data[0] = type, data[1] = session id */
    CONSOLE_OP_HANDOFF_BOOT = 0x703, /* data[1] = tty_id, BOOT -> TTY */
};

enum console_session_type {
    CONSOLE_SESSION_BOOT = 0,
    CONSOLE_SESSION_TTY  = 1,
    CONSOLE_SESSION_GUI  = 2,
};

#endif /* XNIX_PROTOCOL_CONSOLE_H */
