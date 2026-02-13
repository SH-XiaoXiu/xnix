/**
 * @file sudo.h
 * @brief sudod IPC 协议定义
 *
 * sudo 客户端与 sudod 守护进程之间的 IPC 消息格式.
 */

#ifndef D_PROTOCOL_SUDO_H
#define D_PROTOCOL_SUDO_H

/* sudod IPC 操作码 */
#define SUDO_OP_EXEC       0x5001 /* 请求以指定 profile 执行程序 */
#define SUDO_OP_EXEC_REPLY 0x5002 /* 返回 PID */

/*
 * 请求格式:
 *   regs[0] = SUDO_OP_EXEC
 *   buffer  = abi_exec_args
 *   handles = stdin/stdout/stderr (通过 IPC handle 传递)
 *
 * 回复格式:
 *   regs[0] = SUDO_OP_EXEC_REPLY
 *   regs[1] = pid (>0 成功, <0 错误码)
 */

#endif /* D_PROTOCOL_SUDO_H */
