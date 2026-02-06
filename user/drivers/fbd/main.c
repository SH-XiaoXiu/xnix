/**
 * @file main.c
 * @brief fbd (Framebuffer Daemon) - 用户态 Framebuffer 驱动
 *
 * 提供 framebuffer 绘图服务,通过 IPC 接受客户端请求.
 * 使用 HANDLE_PHYSMEM 机制访问 framebuffer.
 */

#include <d/protocol/fb.h>
#include <d/server.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/framebuffer.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

/* 继承的 handles */
#define BOOT_FB_EP 0 /* Framebuffer endpoint */

/* Framebuffer 状态 */
static struct abi_fb_info fb_info;
static uint8_t           *fb_addr  = NULL;
static uint32_t           fb_ready = 0;

/**
 * 计算像素偏移
 */
static inline uint8_t *pixel_addr(int x, int y) {
    return fb_addr + y * fb_info.pitch + x * (fb_info.bpp / 8);
}

/**
 * 将 RGB 值转换为 framebuffer 像素格式
 */
static inline uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0;
    color |= ((uint32_t)r >> (8 - fb_info.red_size)) << fb_info.red_pos;
    color |= ((uint32_t)g >> (8 - fb_info.green_size)) << fb_info.green_pos;
    color |= ((uint32_t)b >> (8 - fb_info.blue_size)) << fb_info.blue_pos;
    return color;
}

/**
 * 画点
 */
static void fb_putpixel(int x, int y, uint32_t color) {
    if (!fb_ready || x < 0 || y < 0 || (uint32_t)x >= fb_info.width ||
        (uint32_t)y >= fb_info.height) {
        return;
    }

    if (fb_info.bpp == 32) {
        uint32_t *pixel = (uint32_t *)pixel_addr(x, y);
        *pixel          = color;
    } else if (fb_info.bpp == 24) {
        uint8_t *pixel = pixel_addr(x, y);
        pixel[0]       = color & 0xFF;
        pixel[1]       = (color >> 8) & 0xFF;
        pixel[2]       = (color >> 16) & 0xFF;
    }
}

/**
 * 填充矩形
 */
static void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_ready) {
        return;
    }

    /* 裁剪到屏幕范围 */
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > (int)fb_info.width) {
        w = (int)fb_info.width - x;
    }
    if (y + h > (int)fb_info.height) {
        h = (int)fb_info.height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    if (fb_info.bpp == 32) {
        for (int row = y; row < y + h; row++) {
            uint32_t *line = (uint32_t *)pixel_addr(x, row);
            for (int i = 0; i < w; i++) {
                line[i] = color;
            }
        }
    } else if (fb_info.bpp == 24) {
        uint8_t b0 = color & 0xFF;
        uint8_t b1 = (color >> 8) & 0xFF;
        uint8_t b2 = (color >> 16) & 0xFF;
        for (int row = y; row < y + h; row++) {
            uint8_t *line = pixel_addr(x, row);
            for (int i = 0; i < w; i++) {
                line[i * 3]     = b0;
                line[i * 3 + 1] = b1;
                line[i * 3 + 2] = b2;
            }
        }
    }
}

/**
 * 滚动屏幕
 */
static void fb_scroll(int lines, uint32_t bg_color) {
    if (!fb_ready || lines <= 0) {
        return;
    }

    if (lines >= (int)fb_info.height) {
        fb_fill_rect(0, 0, fb_info.width, fb_info.height, bg_color);
        return;
    }

    int move_lines = fb_info.height - lines;
    memmove(fb_addr, fb_addr + lines * fb_info.pitch, move_lines * fb_info.pitch);
    fb_fill_rect(0, move_lines, fb_info.width, lines, bg_color);
}

/**
 * 清屏
 */
static void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb_info.width, fb_info.height, color);
}

/**
 * UDM 消息处理
 */
static int fb_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    switch (op) {
    case UDM_FB_GET_INFO:
        /* 返回 framebuffer 信息 */
        msg->regs.data[0] = fb_ready ? UDM_OK : UDM_ERR_UNKNOWN;
        msg->regs.data[1] = fb_info.width;
        msg->regs.data[2] = fb_info.height;
        msg->regs.data[3] = fb_info.pitch;
        msg->regs.data[4] = fb_info.bpp;
        msg->regs.data[5] = ((uint32_t)fb_info.red_pos << 24) | ((uint32_t)fb_info.red_size << 16) |
                            ((uint32_t)fb_info.green_pos << 8) | fb_info.green_size;
        msg->regs.data[6] = ((uint32_t)fb_info.blue_pos << 8) | fb_info.blue_size;
        break;

    case UDM_FB_PUTPIXEL: {
        int      x     = (int)UDM_MSG_ARG(msg, 0);
        int      y     = (int)UDM_MSG_ARG(msg, 1);
        uint32_t color = UDM_MSG_ARG(msg, 2);
        fb_putpixel(x, y, color);
        msg->regs.data[0] = UDM_OK;
        break;
    }

    case UDM_FB_FILL_RECT: {
        int      x     = (int)UDM_MSG_ARG(msg, 0);
        int      y     = (int)UDM_MSG_ARG(msg, 1);
        int      w     = (int)UDM_MSG_ARG(msg, 2);
        int      h     = (int)UDM_MSG_ARG(msg, 3);
        uint32_t color = UDM_MSG_ARG(msg, 4);
        fb_fill_rect(x, y, w, h, color);
        msg->regs.data[0] = UDM_OK;
        break;
    }

    case UDM_FB_SCROLL: {
        int      lines    = (int)UDM_MSG_ARG(msg, 0);
        uint32_t bg_color = UDM_MSG_ARG(msg, 1);
        fb_scroll(lines, bg_color);
        msg->regs.data[0] = UDM_OK;
        break;
    }

    case UDM_FB_CLEAR: {
        uint32_t color = UDM_MSG_ARG(msg, 0);
        fb_clear(color);
        msg->regs.data[0] = UDM_OK;
        break;
    }

    default:
        msg->regs.data[0] = UDM_ERR_INVALID;
        break;
    }

    return 0;
}

int main(void) {
    printf("[fbd] Starting framebuffer driver\n");

    /* 查找 framebuffer physmem handle */
    handle_t fb_handle = sys_handle_find("fb_mem");
    if (fb_handle == HANDLE_INVALID) {
        printf("[fbd] Failed to find fb_mem handle\n");
        return 1;
    }
    printf("[fbd] Found fb_mem handle: %u\n", fb_handle);

    /* 获取 framebuffer 信息 */
    struct physmem_info pinfo;
    if (sys_physmem_info(fb_handle, &pinfo) < 0) {
        printf("[fbd] Failed to get physmem info\n");
        return 1;
    }

    if (pinfo.type != 1) { /* PHYSMEM_TYPE_FB */
        printf("[fbd] fb_mem is not a framebuffer type\n");
        return 1;
    }

    /* 填充 fb_info */
    fb_info.width      = pinfo.width;
    fb_info.height     = pinfo.height;
    fb_info.pitch      = pinfo.pitch;
    fb_info.bpp        = pinfo.bpp;
    fb_info.red_pos    = pinfo.red_pos;
    fb_info.red_size   = pinfo.red_size;
    fb_info.green_pos  = pinfo.green_pos;
    fb_info.green_size = pinfo.green_size;
    fb_info.blue_pos   = pinfo.blue_pos;
    fb_info.blue_size  = pinfo.blue_size;

    printf("[fbd] Framebuffer: %ux%u, %u bpp, pitch=%u\n", fb_info.width, fb_info.height,
           fb_info.bpp, fb_info.pitch);

    /* 映射 framebuffer 到用户空间 */
    fb_addr = (uint8_t *)sys_mmap_phys(fb_handle, 0, 0, 0x03, NULL); /* PROT_READ | PROT_WRITE */
    if ((int)(uintptr_t)fb_addr < 0 || fb_addr == NULL) {
        printf("[fbd] Failed to map framebuffer\n");
        return 1;
    }

    printf("[fbd] Framebuffer mapped at %p\n", fb_addr);

    fb_ready = 1;

    /* 启动 UDM 服务 */
    struct udm_server srv = {
        .endpoint = BOOT_FB_EP,
        .handler  = fb_handler,
        .name     = "fbd",
    };

    udm_server_init(&srv);
    svc_notify_ready("fbd");
    printf("[fbd] Ready, serving on endpoint %u\n", BOOT_FB_EP);

    udm_server_run(&srv);

    return 0;
}
