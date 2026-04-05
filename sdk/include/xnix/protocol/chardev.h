/**
 * @file xnix/protocol/chardev.h
 * @brief 字符设备 IPC 协议
 *
 * 定义字符设备 (串口, 虚拟终端等) 的标准通信协议.
 * 驱动实现此协议, 消费者 (termd) 通过此协议读写数据.
 *
 * Opcode 范围: 0x200-0x2FF
 */

#ifndef XNIX_PROTOCOL_CHARDEV_H
#define XNIX_PROTOCOL_CHARDEV_H

#include <stdint.h>

/* ============== 操作码 ============== */

/**
 * CHARDEV_READ - 阻塞读取数据
 *
 * Request:
 *   data[0] = CHARDEV_READ
 *   data[1] = max_size (最多读取字节数)
 *
 * Reply:
 *   data[0] = 读取的字节数 (>0) 或负错误码
 *   buffer  = 读取的数据
 */
#define CHARDEV_READ  0x200

/**
 * CHARDEV_WRITE - 写入数据
 *
 * Request:
 *   data[0] = CHARDEV_WRITE
 *   data[1] = size (写入字节数)
 *   buffer  = 写入的数据
 *
 * Reply:
 *   data[0] = 实际写入的字节数 (>0) 或负错误码
 */
#define CHARDEV_WRITE 0x201

/**
 * CHARDEV_IOCTL - 设备控制
 *
 * Request:
 *   data[0] = CHARDEV_IOCTL
 *   data[1] = cmd (CHARDEV_IOCTL_*)
 *   data[2..] = cmd 参数
 *
 * Reply:
 *   data[0] = 0 成功, 负错误码失败
 *   data[1..] = cmd 返回值
 */
#define CHARDEV_IOCTL 0x202

/**
 * CHARDEV_INFO - 查询设备信息
 *
 * Request:
 *   data[0] = CHARDEV_INFO
 *
 * Reply:
 *   data[0] = 0 成功
 *   data[1] = caps (CHARDEV_CAP_*)
 *   data[2] = 设备实例号
 *   buffer  = 设备名 (null-terminated)
 */
#define CHARDEV_INFO  0x203

/* ============== IOCTL 命令 ============== */

#define CHARDEV_IOCTL_GET_BAUD  0x01 /* 获取波特率 → data[1] = baud */
#define CHARDEV_IOCTL_SET_BAUD  0x02 /* 设置波特率: data[2] = baud */
#define CHARDEV_IOCTL_FLUSH     0x03 /* 刷新缓冲区 */

/* ============== 能力标志 ============== */

#define CHARDEV_CAP_READ  (1 << 0) /* 设备支持读取 */
#define CHARDEV_CAP_WRITE (1 << 1) /* 设备支持写入 */

/* ============== 常量 ============== */

/** 单次 CHARDEV_READ/WRITE 最大传输字节数 */
#define CHARDEV_IO_MAX 4096

#endif /* XNIX_PROTOCOL_CHARDEV_H */
