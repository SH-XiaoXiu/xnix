/**
 * @file xnix/protocol/tty.h
 * @brief TTY IPC 协议定义
 *
 * 定义用户态程序与 ttyd 终端服务器之间的通信协议.
 *
 * 当前主路径:
 *   打开后的 TTY 对象统一走 IO_READ / IO_WRITE / IO_CLOSE / IO_IOCTL.
 *
 */

#ifndef XNIX_PROTOCOL_TTY_H
#define XNIX_PROTOCOL_TTY_H

#include <stdint.h>
#include <xnix/protocol/udm_errno.h>

/* Helper macros for message parsing */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/**
 * TTY 操作码
 */
enum tty_op {
    TTY_OP_INPUT  = 7, /* 内部输入注入: kbd/seriald -> ttyd */
    TTY_OP_CREATE = 8, /* 管理面: 动态创建 TTY
                          * req: handles[0]=output_ep (可选,缺省走 serial)
                          * rep: data[1]=tty_id, handles[0]=new tty endpoint */
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

/* TTY 写缓冲区可通过寄存器传输的最大字节数 */
#define TTY_WRITE_MAX_INLINE 24 /* 6*4 bytes from data[2..7] */

/* TTY 输入缓冲区大小 */
#define TTY_INPUT_BUF_SIZE 256

#endif /* XNIX_PROTOCOL_TTY_H */
