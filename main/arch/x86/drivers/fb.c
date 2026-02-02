/**
 * @file fb.c
 * @brief Framebuffer 底层驱动
 *
 * 提供像素级操作接口,由 fb_console 使用进行字符渲染.
 *
 * 由于 framebuffer 通常位于高物理地址(超出启动时映射范围),
 * 需要在 VMM 初始化后才能映射.fb_late_init() 负责实际映射.
 */

#include <asm/mmu.h>
#include <xnix/boot.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/types.h>
#include <xnix/vmm.h>

/* Framebuffer 状态 */
static uint32_t *fb_addr   = NULL;
static uint32_t  fb_width  = 0;
static uint32_t  fb_height = 0;
static uint32_t  fb_pitch  = 0; /* 每行字节数 */
static uint8_t   fb_bpp    = 0;

/* Framebuffer 信息(用于延迟映射) */
static uint64_t fb_phys_addr  = 0;
static uint32_t fb_size       = 0;
static bool     fb_info_saved = false;

/* RGB 位域信息 */
static uint8_t fb_red_pos    = 16;
static uint8_t fb_red_size   = 8;
static uint8_t fb_green_pos  = 8;
static uint8_t fb_green_size = 8;
static uint8_t fb_blue_pos   = 0;
static uint8_t fb_blue_size  = 8;

bool fb_available(void) {
    return fb_addr != NULL;
}

bool fb_info_available(void) {
    return fb_info_saved;
}

uint32_t fb_get_width(void) {
    return fb_width;
}

uint32_t fb_get_height(void) {
    return fb_height;
}

/**
 * 将 RGB 值转换为 framebuffer 像素格式
 */
static inline uint32_t fb_make_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = 0;
    color |= ((uint32_t)r >> (8 - fb_red_size)) << fb_red_pos;
    color |= ((uint32_t)g >> (8 - fb_green_size)) << fb_green_pos;
    color |= ((uint32_t)b >> (8 - fb_blue_size)) << fb_blue_pos;
    return color;
}

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return fb_make_color(r, g, b);
}

void fb_putpixel(int x, int y, uint32_t color) {
    if (!fb_addr || x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height) {
        return;
    }

    if (fb_bpp == 32) {
        uint32_t *pixel = (uint32_t *)((uint8_t *)fb_addr + y * fb_pitch + x * 4);
        *pixel          = color;
    } else if (fb_bpp == 24) {
        uint8_t *pixel = (uint8_t *)fb_addr + y * fb_pitch + x * 3;
        pixel[0]       = color & 0xFF;
        pixel[1]       = (color >> 8) & 0xFF;
        pixel[2]       = (color >> 16) & 0xFF;
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_addr) {
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
    if (x + w > (int)fb_width) {
        w = (int)fb_width - x;
    }
    if (y + h > (int)fb_height) {
        h = (int)fb_height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    if (fb_bpp == 32) {
        for (int row = y; row < y + h; row++) {
            uint32_t *line = (uint32_t *)((uint8_t *)fb_addr + row * fb_pitch) + x;
            for (int i = 0; i < w; i++) {
                line[i] = color;
            }
        }
    } else if (fb_bpp == 24) {
        uint8_t b0 = color & 0xFF;
        uint8_t b1 = (color >> 8) & 0xFF;
        uint8_t b2 = (color >> 16) & 0xFF;
        for (int row = y; row < y + h; row++) {
            uint8_t *line = (uint8_t *)fb_addr + row * fb_pitch + x * 3;
            for (int i = 0; i < w; i++) {
                line[i * 3]     = b0;
                line[i * 3 + 1] = b1;
                line[i * 3 + 2] = b2;
            }
        }
    }
}

void fb_scroll(int lines, int char_height, uint32_t bg_color) {
    if (!fb_addr || lines <= 0) {
        return;
    }

    int scroll_pixels = lines * char_height;
    if (scroll_pixels >= (int)fb_height) {
        /* 滚动超过整屏,直接清屏 */
        fb_fill_rect(0, 0, fb_width, fb_height, bg_color);
        return;
    }

    /* 向上移动像素 */
    int bytes_per_line = fb_pitch;
    int move_lines     = fb_height - scroll_pixels;

    memmove(fb_addr, (uint8_t *)fb_addr + scroll_pixels * bytes_per_line,
            move_lines * bytes_per_line);

    /* 清除底部空出的区域 */
    fb_fill_rect(0, move_lines, fb_width, scroll_pixels, bg_color);
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb_width, fb_height, color);
}

void fb_draw_glyph(int px, int py, const uint8_t *glyph, int glyph_width, int glyph_height,
                   uint32_t fg, uint32_t bg) {
    if (!fb_addr || !glyph) {
        return;
    }

    /* 边界检查(整个字形) */
    if (px < 0 || py < 0 || (uint32_t)(px + glyph_width) > fb_width ||
        (uint32_t)(py + glyph_height) > fb_height) {
        return;
    }

    if (fb_bpp == 32) {
        for (int row = 0; row < glyph_height; row++) {
            uint32_t *line = (uint32_t *)((uint8_t *)fb_addr + (py + row) * fb_pitch) + px;
            for (int col = 0; col < glyph_width; col++) {
                int byte_idx, bit_idx;
                if (glyph_width <= 8) {
                    byte_idx = row;
                    bit_idx  = 7 - col;
                } else {
                    byte_idx = row * 2 + col / 8;
                    bit_idx  = 7 - (col % 8);
                }
                line[col] = (glyph[byte_idx] & (1 << bit_idx)) ? fg : bg;
            }
        }
    } else if (fb_bpp == 24) {
        for (int row = 0; row < glyph_height; row++) {
            uint8_t *line = (uint8_t *)fb_addr + (py + row) * fb_pitch + px * 3;
            for (int col = 0; col < glyph_width; col++) {
                int byte_idx, bit_idx;
                if (glyph_width <= 8) {
                    byte_idx = row;
                    bit_idx  = 7 - col;
                } else {
                    byte_idx = row * 2 + col / 8;
                    bit_idx  = 7 - (col % 8);
                }
                uint32_t color    = (glyph[byte_idx] & (1 << bit_idx)) ? fg : bg;
                line[col * 3]     = color & 0xFF;
                line[col * 3 + 1] = (color >> 8) & 0xFF;
                line[col * 3 + 2] = (color >> 16) & 0xFF;
            }
        }
    }
}

void fb_init(void) {
    struct boot_framebuffer_info info;

    if (boot_get_framebuffer(&info) < 0) {
        return;
    }

    /* 只支持 RGB 类型的 framebuffer */
    if (info.type != 1) {
        return;
    }

    /* 只支持 24/32 位色深 */
    if (info.bpp != 24 && info.bpp != 32) {
        return;
    }

    /* 保存 framebuffer 信息,延迟到 VMM 初始化后映射 */
    fb_phys_addr = info.addr;
    fb_width     = info.width;
    fb_height    = info.height;
    fb_pitch     = info.pitch;
    fb_bpp       = info.bpp;
    fb_size      = fb_pitch * fb_height;

    fb_red_pos    = info.red_pos;
    fb_red_size   = info.red_size;
    fb_green_pos  = info.green_pos;
    fb_green_size = info.green_size;
    fb_blue_pos   = info.blue_pos;
    fb_blue_size  = info.blue_size;

    fb_info_saved = true;
}

/* fb_console 初始化回调(由 fb_late_init 调用) */
static void (*fb_console_late_init_cb)(void) = NULL;

void fb_set_console_init_callback(void (*cb)(void)) {
    fb_console_late_init_cb = cb;
}

void fb_late_init(void) {
    /* 首先从 boot 获取 framebuffer 信息(此时 boot_init 已完成) */
    if (!fb_info_saved) {
        fb_init();
    }

    if (!fb_info_saved || fb_addr) {
        return;
    }

    /* 映射 framebuffer 内存区域 */
    uint32_t phys_base = (uint32_t)fb_phys_addr;
    uint32_t phys_end  = phys_base + fb_size;

    /* 页对齐 */
    phys_base &= ~0xFFF;
    phys_end = (phys_end + 0xFFF) & ~0xFFF;

    /* 映射所有页(不使用 NOCACHE,允许 write-back 缓存提高性能) */
    for (uint32_t phys = phys_base; phys < phys_end; phys += 4096) {
        if (vmm_map_page(NULL, phys, phys, VMM_PROT_READ | VMM_PROT_WRITE) < 0) {
            pr_err("FB: Failed to map page at 0x%x", phys);
            return;
        }
    }

    /* 设置虚拟地址(恒等映射) */
    fb_addr = (uint32_t *)(uint32_t)fb_phys_addr;

    pr_info("FB: Mapped %ux%u@%u at 0x%x, RGB pos=%u/%u/%u size=%u/%u/%u", fb_width, fb_height,
            fb_bpp, (uint32_t)fb_phys_addr, fb_red_pos, fb_green_pos, fb_blue_pos, fb_red_size,
            fb_green_size, fb_blue_size);

    /* 调用 fb_console 初始化 */
    if (fb_console_late_init_cb) {
        fb_console_late_init_cb();
    }
}
