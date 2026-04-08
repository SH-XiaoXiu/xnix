/**
 * @file xnix/protocol/displaydev.h
 * @brief 显示设备 IPC 协议
 *
 * 定义文本显示设备 (VGA 文本, framebuffer 文本控制台等) 的标准通信协议.
 * 驱动实现此协议, 消费者 (termd) 通过此协议输出文本.
 *
 * 与 protocol/fb.h (framebuffer 像素协议) 的关系:
 *   - fb.h 定义像素级操作 (putpixel, fill_rect, scroll)
 *   - displaydev.h 定义文本级操作 (write, clear, set_cursor, set_color)
 *   fbcond 驱动内部用 fb.h 渲染, 对外暴露 displaydev.h 协议.
 *
 * Opcode 范围: 0x400-0x4FF
 */

#ifndef XNIX_PROTOCOL_DISPLAYDEV_H
#define XNIX_PROTOCOL_DISPLAYDEV_H

#include <stdint.h>

/* ============== 操作码 ============== */

/**
 * DISPDEV_WRITE - 写入文本
 *
 * Request:
 *   data[0] = DISPDEV_WRITE
 *   data[1] = size (字节数)
 *   buffer  = 文本数据 (支持 \n, \r, \b, \t)
 *
 * Reply:
 *   data[0] = 实际写入的字节数 (>0) 或负错误码
 */
#define DISPDEV_WRITE     0x400

/**
 * DISPDEV_CLEAR - 清屏
 *
 * Request:
 *   data[0] = DISPDEV_CLEAR
 *
 * Reply:
 *   data[0] = 0 成功
 */
#define DISPDEV_CLEAR     0x401

/**
 * DISPDEV_SCROLL - 滚动
 *
 * Request:
 *   data[0] = DISPDEV_SCROLL
 *   data[1] = lines (正数向上滚动)
 *
 * Reply:
 *   data[0] = 0 成功
 */
#define DISPDEV_SCROLL    0x402

/**
 * DISPDEV_SET_CURSOR - 设置光标位置
 *
 * Request:
 *   data[0] = DISPDEV_SET_CURSOR
 *   data[1] = row
 *   data[2] = col
 *
 * Reply:
 *   data[0] = 0 成功
 */
#define DISPDEV_SET_CURSOR 0x403

/**
 * DISPDEV_SET_COLOR - 设置前景/背景颜色
 *
 * Request:
 *   data[0] = DISPDEV_SET_COLOR
 *   data[1] = fg (低 4 位) | (bg << 4)
 *
 * Reply:
 *   data[0] = 0 成功
 */
#define DISPDEV_SET_COLOR  0x404

/**
 * DISPDEV_RESET_COLOR - 重置为默认颜色
 *
 * Request:
 *   data[0] = DISPDEV_RESET_COLOR
 *
 * Reply:
 *   data[0] = 0 成功
 */
#define DISPDEV_RESET_COLOR 0x405

/**
 * DISPDEV_DRAW_CURSOR - 在指定位置渲染可见光标
 *
 * 与 DISPDEV_SET_CURSOR 的区别:
 *   DISPDEV_SET_CURSOR 仅移动写入位置 (渲染循环中用).
 *   DISPDEV_DRAW_CURSOR 在屏幕上实际绘制光标 (flush 结束时用).
 *
 * Request:
 *   data[0] = DISPDEV_DRAW_CURSOR
 *   data[1] = row
 *   data[2] = col
 *
 * Reply:
 *   data[0] = 0 成功
 */
#define DISPDEV_DRAW_CURSOR 0x407

/**
 * DISPDEV_INFO - 查询设备信息
 *
 * Request:
 *   data[0] = DISPDEV_INFO
 *
 * Reply:
 *   data[0] = 0 成功
 *   data[1] = caps (DISPDEV_CAP_*)
 *   data[2] = rows (文本行数)
 *   data[3] = cols (文本列数)
 *   data[4] = 设备实例号
 *   buffer  = 设备名 (null-terminated)
 */
#define DISPDEV_INFO       0x406

/* ============== 能力标志 ============== */

#define DISPDEV_CAP_COLOR  (1 << 0) /* 支持颜色 */
#define DISPDEV_CAP_CURSOR (1 << 1) /* 支持光标定位 */
#define DISPDEV_CAP_SCROLL (1 << 2) /* 支持滚动 */

/* ============== 常量 ============== */

/** 单次 DISPDEV_WRITE 最大传输字节数 */
#define DISPDEV_WRITE_MAX 4096

#endif /* XNIX_PROTOCOL_DISPLAYDEV_H */
