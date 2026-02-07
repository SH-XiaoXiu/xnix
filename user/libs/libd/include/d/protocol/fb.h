/**
 * @file d/protocol/fb.h
 * @brief Framebuffer 协议定义
 */

#ifndef D_PROTOCOL_FB_H
#define D_PROTOCOL_FB_H

#include <stdint.h>
#include <xnix/abi/framebuffer.h>
#include <xnix/abi/protocol.h>

/* Helper macros for message parsing */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/**
 * Framebuffer 操作码
 */
enum udm_fb_op {
    UDM_FB_GET_INFO  = 1, /* 获取信息: 返回 fb_info 结构 */
    UDM_FB_PUTPIXEL  = 2, /* 画点: data[1]=x, data[2]=y, data[3]=color */
    UDM_FB_FILL_RECT = 3, /* 填充矩形: data[1]=x, data[2]=y, data[3]=w, data[4]=h, data[5]=color */
    UDM_FB_SCROLL    = 4, /* 滚动: data[1]=lines (正数向上) */
    UDM_FB_CLEAR     = 5, /* 清屏: data[1]=color (0 表示黑色) */
    UDM_FB_BLIT      = 6, /* 位图传输(预留) */
};

/**
 * GET_INFO 响应消息布局
 * reply.regs.data[0] = 0 或错误码
 * reply.regs.data[1] = width
 * reply.regs.data[2] = height
 * reply.regs.data[3] = pitch
 * reply.regs.data[4] = bpp
 * reply.regs.data[5] = (red_pos << 24) | (red_size << 16) | (green_pos << 8) | green_size
 * reply.regs.data[6] = (blue_pos << 8) | blue_size
 */

#endif /* D_PROTOCOL_FB_H */
