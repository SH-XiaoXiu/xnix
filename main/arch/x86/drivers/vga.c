/**
 * @file vga.c
 * @brief x86 VGA 文本模式驱动
 * @author XiaoXiu
 * @date 2026-01-20
 */

#include <arch/cpu.h>

#include <xnix/console.h>
#include <xnix/types.h>

/* 检查 framebuffer 信息是否存在(由 fb.c 提供) */
extern bool fb_info_available(void);

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA CRT 控制器端口 */
#define VGA_CRTC_INDEX   0x3D4
#define VGA_CRTC_DATA    0x3D5
#define VGA_CURSOR_HIGH  0x0E
#define VGA_CURSOR_LOW   0x0F
#define VGA_CURSOR_START 0x0A
#define VGA_CURSOR_END   0x0B

/* VGA 颜色 */
enum vga_color {
    VGA_BLACK       = 0,
    VGA_BLUE        = 1,
    VGA_GREEN       = 2,
    VGA_CYAN        = 3,
    VGA_RED         = 4,
    VGA_MAGENTA     = 5,
    VGA_BROWN       = 6,
    VGA_LIGHT_GREY  = 7,
    VGA_DARK_GREY   = 8,
    VGA_LIGHT_BLUE  = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN  = 11,
    VGA_LIGHT_RED   = 12,
    VGA_PINK        = 13,
    VGA_YELLOW      = 14,
    VGA_WHITE       = 15,
};

static uint16_t *vga_buffer;
static int       vga_x, vga_y;
static uint8_t   vga_attr;

/* ANSI 解析状态 */
enum ansi_state {
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
};

static enum ansi_state ansi_state = ANSI_NORMAL;
static int             ansi_params[4];
static int             ansi_param_count;
static int             ansi_param_value;

static inline uint8_t make_attr(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

/* 更新硬件光标位置 */
static void vga_update_cursor(void) {
    uint16_t pos = vga_y * VGA_WIDTH + vga_x;
    outb(VGA_CRTC_INDEX, VGA_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (pos >> 8) & 0xFF);
    outb(VGA_CRTC_INDEX, VGA_CURSOR_LOW);
    outb(VGA_CRTC_DATA, pos & 0xFF);
}

/* 启用光标(扫描线 14-15,下划线样式) */
static void vga_enable_cursor(void) {
    outb(VGA_CRTC_INDEX, VGA_CURSOR_START);
    outb(VGA_CRTC_DATA, 14);
    outb(VGA_CRTC_INDEX, VGA_CURSOR_END);
    outb(VGA_CRTC_DATA, 15);
}

static inline uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)c | ((uint16_t)attr << 8);
}

static void vga_scroll(void) {
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_entry(' ', vga_attr);
    }
}

static void vga_init(void) {
    vga_buffer = (uint16_t *)VGA_BUFFER;
    vga_x      = 0;
    vga_y      = 0;
    vga_attr   = make_attr(VGA_LIGHT_GREY, VGA_BLACK);
    vga_enable_cursor();
    vga_update_cursor();
}

/* 前向声明 */
static void vga_clear(void);

/* ANSI 颜色码到 VGA 颜色的映射 */
static const uint8_t ansi_to_vga[] = {
    VGA_BLACK,      /* 0 -> 30 */
    VGA_RED,        /* 1 -> 31 */
    VGA_GREEN,      /* 2 -> 32 */
    VGA_BROWN,      /* 3 -> 33 (yellow) */
    VGA_BLUE,       /* 4 -> 34 */
    VGA_MAGENTA,    /* 5 -> 35 */
    VGA_CYAN,       /* 6 -> 36 */
    VGA_LIGHT_GREY, /* 7 -> 37 */
};

static const uint8_t ansi_to_vga_bright[] = {
    VGA_DARK_GREY,   /* 0 -> 90 or 1;30 */
    VGA_LIGHT_RED,   /* 1 -> 91 or 1;31 */
    VGA_LIGHT_GREEN, /* 2 -> 92 or 1;32 */
    VGA_YELLOW,      /* 3 -> 93 or 1;33 */
    VGA_LIGHT_BLUE,  /* 4 -> 94 or 1;34 */
    VGA_PINK,        /* 5 -> 95 or 1;35 */
    VGA_LIGHT_CYAN,  /* 6 -> 96 or 1;36 */
    VGA_WHITE,       /* 7 -> 97 or 1;37 */
};

/* 处理 ANSI SGR (Select Graphic Rendition) 命令 */
static void ansi_handle_sgr(void) {
    int bold = 0;

    for (int i = 0; i < ansi_param_count; i++) {
        int p = ansi_params[i];
        if (p == 0) {
            /* 重置 */
            vga_attr = make_attr(VGA_LIGHT_GREY, VGA_BLACK);
            bold     = 0;
        } else if (p == 1) {
            /* 粗体/亮色 */
            bold = 1;
        } else if (p >= 30 && p <= 37) {
            /* 标准前景色 */
            uint8_t color = bold ? ansi_to_vga_bright[p - 30] : ansi_to_vga[p - 30];
            vga_attr      = make_attr(color, VGA_BLACK);
        } else if (p >= 90 && p <= 97) {
            /* 亮色前景 */
            vga_attr = make_attr(ansi_to_vga_bright[p - 90], VGA_BLACK);
        }
    }

    /* 如果没有参数,默认为重置 */
    if (ansi_param_count == 0) {
        vga_attr = make_attr(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* 处理 ANSI CSI 序列结束 */
static void ansi_handle_csi(char cmd) {
    /* 保存最后一个参数 */
    if (ansi_param_count < 4) {
        ansi_params[ansi_param_count++] = ansi_param_value;
    }

    switch (cmd) {
    case 'm':
        ansi_handle_sgr();
        break;
    case 'H':
    case 'f':
        /* 光标位置 - 简单实现 */
        vga_y = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
        vga_x = (ansi_param_count > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
        if (vga_y >= VGA_HEIGHT) {
            vga_y = VGA_HEIGHT - 1;
        }
        if (vga_x >= VGA_WIDTH) {
            vga_x = VGA_WIDTH - 1;
        }
        vga_update_cursor();
        break;
    case 'J':
        /* 清屏 */
        if (ansi_params[0] == 2) {
            vga_clear();
        }
        break;
    default:
        /* 忽略未知命令 */
        break;
    }
}

static void vga_putc_raw(char c) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else if (c == '\b') {
        if (vga_x > 0) {
            vga_x--;
        }
    } else {
        vga_buffer[vga_y * VGA_WIDTH + vga_x] = make_entry(c, vga_attr);
        vga_x++;
    }

    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }
    if (vga_y >= VGA_HEIGHT) {
        vga_scroll();
        vga_y = VGA_HEIGHT - 1;
    }
    vga_update_cursor();
}

static void vga_putc(char c) {
    /* 如果 framebuffer 可用,VGA 不输出(避免双重输出) */
    if (fb_info_available()) {
        return;
    }

    switch (ansi_state) {
    case ANSI_NORMAL:
        if (c == '\x1b') {
            ansi_state = ANSI_ESC;
        } else {
            vga_putc_raw(c);
        }
        break;

    case ANSI_ESC:
        if (c == '[') {
            ansi_state       = ANSI_CSI;
            ansi_param_count = 0;
            ansi_param_value = 0;
        } else {
            /* 不是 CSI 序列,回到正常模式 */
            ansi_state = ANSI_NORMAL;
        }
        break;

    case ANSI_CSI:
        if (c >= '0' && c <= '9') {
            ansi_param_value = ansi_param_value * 10 + (c - '0');
        } else if (c == ';') {
            if (ansi_param_count < 4) {
                ansi_params[ansi_param_count++] = ansi_param_value;
            }
            ansi_param_value = 0;
        } else if (c >= 0x40 && c <= 0x7E) {
            /* 命令字符 */
            ansi_handle_csi(c);
            ansi_state = ANSI_NORMAL;
        } else {
            /* 无效字符,放弃解析 */
            ansi_state = ANSI_NORMAL;
        }
        break;
    }
}

static void vga_puts(const char *s) {
    if (fb_info_available()) {
        return;
    }
    while (*s) {
        vga_putc(*s++);
    }
}

static void vga_clear(void) {
    if (fb_info_available()) {
        return;
    }
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = make_entry(' ', vga_attr);
    }
    vga_x = 0;
    vga_y = 0;
    vga_update_cursor();
}

/* 导出驱动结构 */
static struct console vga_console = {
    .name        = "vga",
    .flags       = CONSOLE_SYNC,
    .init        = vga_init,
    .putc        = vga_putc,
    .puts        = vga_puts,
    .set_color   = NULL, /* 颜色通过 ANSI 序列处理 */
    .reset_color = NULL, /* 颜色通过 ANSI 序列处理 */
    .clear       = vga_clear,
};

void vga_console_register(void) {
    console_register(&vga_console);
}
