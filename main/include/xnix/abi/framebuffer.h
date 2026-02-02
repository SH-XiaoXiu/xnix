/**
 * @file abi/framebuffer.h
 * @brief Framebuffer ABI 结构定义
 */

#ifndef XNIX_ABI_FRAMEBUFFER_H
#define XNIX_ABI_FRAMEBUFFER_H

#include <xnix/abi/types.h>

/**
 * Framebuffer 信息(ABI 结构)
 *
 * 与内核 boot_framebuffer_info 相似,但用于用户空间 ABI.
 */
struct abi_fb_info {
    uint32_t width;      /* 宽度(像素) */
    uint32_t height;     /* 高度(像素) */
    uint32_t pitch;      /* 每行字节数 */
    uint32_t bpp;        /* 每像素位数(8/16/24/32) */
    uint8_t  red_pos;    /* 红色位位置 */
    uint8_t  red_size;   /* 红色位长度 */
    uint8_t  green_pos;  /* 绿色位位置 */
    uint8_t  green_size; /* 绿色位长度 */
    uint8_t  blue_pos;   /* 蓝色位位置 */
    uint8_t  blue_size;  /* 蓝色位长度 */
    uint8_t  _pad[2];    /* 填充对齐 */
};

/**
 * 用户空间 framebuffer 映射基地址
 *
 * 选择 0x40000000 (1GB) 以避免与堆和栈冲突.
 */
#define ABI_FB_MAP_BASE 0x40000000

#endif /* XNIX_ABI_FRAMEBUFFER_H */
