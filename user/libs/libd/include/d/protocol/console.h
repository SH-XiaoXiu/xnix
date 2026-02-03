/**
 * @file udm/protocol/console.h
 * @brief Console Protocol Definition (userspace only)
 *
 * Defines IPC protocol for console drivers (seriald, kbd).
 * Used by both client (libc) and server (drivers) code.
 */

#ifndef UDM_PROTOCOL_CONSOLE_H
#define UDM_PROTOCOL_CONSOLE_H

#include <stdint.h>

/* Console Protocol Operation Codes */
#define UDM_CONSOLE_PUTC  1
#define UDM_CONSOLE_WRITE 2
#define UDM_CONSOLE_CLEAR 3

/* Console write max (inline in message) */
#define UDM_CONSOLE_WRITE_MAX 24

/* Helper macros for message parsing */
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])

#endif /* UDM_PROTOCOL_CONSOLE_H */
