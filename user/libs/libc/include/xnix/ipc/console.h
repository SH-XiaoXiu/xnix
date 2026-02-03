/**
 * @file xnix/ipc/console.h
 * @brief 控制台 IPC 协议定义
 *
 * 定义用户态程序与控制台驱动(seriald, kbd)之间的通信协议.
 */

#ifndef XNIX_IPC_CONSOLE_H
#define XNIX_IPC_CONSOLE_H

#include <stdint.h>

/**
 * 控制台操作码
 */
typedef enum {
    CONSOLE_OP_PUTC           = 1, /* 输出单个字符 */
    CONSOLE_OP_WRITE          = 2, /* 输出字符串 */
    CONSOLE_OP_GETC           = 3, /* 读取单个字符(阻塞) */
    CONSOLE_OP_READ           = 4, /* 读取多个字符 */
    CONSOLE_OP_POLL           = 5, /* 非阻塞检查是否有输入 */
    CONSOLE_OP_FLUSH          = 6, /* 刷新输出缓冲区 */
    CONSOLE_OP_SET_FOREGROUND = 7, /* 设置前台进程(用于 Ctrl+C) */
} console_op_t;

/**
 * 标准 IPC 消息格式 (控制台)
 *
 * 请求格式:
 *   - data[0]: op_code (console_op_t)
 *   - data[1]: char (PUTC) 或 buffer pointer (WRITE/READ)
 *   - data[2]: size (WRITE/READ)
 *
 * 回复格式:
 *   - data[0]: result (字符值或字节数, <0 错误码)
 */
struct console_ipc_request {
    uint32_t op_code; /* 操作码 (console_op_t) */
    uint32_t data1;   /* 参数1 (字符或缓冲区指针) */
    uint32_t data2;   /* 参数2 (大小) */
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
};

struct console_ipc_response {
    int32_t  result; /* 返回值 (字符/字节数, <0 错误) */
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
    uint32_t reserved6;
    uint32_t reserved7;
};

/**
 * 控制台缓冲区最大长度
 */
#define CONSOLE_BUF_MAX 4096

#endif /* XNIX_IPC_CONSOLE_H */
