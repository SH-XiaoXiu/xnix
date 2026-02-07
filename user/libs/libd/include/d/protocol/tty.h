/**
 * @file d/protocol/tty.h
 * @brief TTY IPC 协议定义
 *
 * 定义用户态程序与 ttyd 终端服务器之间的通信协议.
 */

#ifndef D_PROTOCOL_TTY_H
#define D_PROTOCOL_TTY_H

#include <stdint.h>
#include <xnix/abi/protocol.h>

/* Helper macros for message parsing */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/**
 * TTY 操作码
 */
enum tty_op {
    TTY_OP_OPEN  = 1, /* 打开 tty session: data[1]=tty_id */
    TTY_OP_WRITE = 2, /* 写输出: data[1..]=bytes (通过 buffer) */
    TTY_OP_READ  = 3, /* 读输入(阻塞): data[1]=max_len */
    TTY_OP_IOCTL = 4, /* 终端控制: data[1]=cmd, data[2..]=args */
    TTY_OP_CLOSE = 5, /* 关闭 session */
    TTY_OP_PUTC  = 6, /* 输出单个字符: data[1]=char */
    TTY_OP_INPUT = 7, /* 输入设备推送字符: data[1]=char (kbd/seriald → ttyd) */
};

/**
 * TTY ioctl 命令
 */
enum tty_ioctl {
    TTY_IOCTL_SET_FOREGROUND = 1, /* 设置前台 PID: data[2]=pid */
    TTY_IOCTL_GET_FOREGROUND = 2, /* 获取前台 PID */
    TTY_IOCTL_SET_RAW        = 3, /* 切换到 raw 模式 */
    TTY_IOCTL_SET_COOKED     = 4, /* 切换到 cooked 模式 */
    TTY_IOCTL_SET_ECHO       = 5, /* echo 开关: data[2]=0/1 */
    TTY_IOCTL_GET_TTY_COUNT  = 6, /* 查询 tty 数量 */
    TTY_IOCTL_SET_COLOR      = 7, /* 设置颜色: data[2]=fg, data[3]=bg (VGA 16 色) */
    TTY_IOCTL_RESET_COLOR    = 8, /* 重置颜色 */
};

/**
 * TTY WRITE 消息格式
 *
 * 请求:
 *   data[0] = TTY_OP_WRITE
 *   data[1] = length
 *   buffer  = 字符数据
 *
 * 回复:
 *   data[0] = 写入的字节数,负数为错误
 */

/**
 * TTY READ 消息格式
 *
 * 请求:
 *   data[0] = TTY_OP_READ
 *   data[1] = max_len (最大读取长度)
 *
 * 回复:
 *   data[0] = 实际读取字节数,负数为错误
 *   buffer  = 读取的数据
 */

/* TTY 写缓冲区可通过寄存器传输的最大字节数 */
#define TTY_WRITE_MAX_INLINE 24 /* 6*4 bytes from data[2..7] */

/* TTY 输入缓冲区大小 */
#define TTY_INPUT_BUF_SIZE 256

#endif /* D_PROTOCOL_TTY_H */
