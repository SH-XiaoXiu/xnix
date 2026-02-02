/**
 * @file fb_console.c
 * @brief Framebuffer 控制台驱动
 *
 * 在 framebuffer 上渲染字符,支持 UTF-8 解码和中文显示.
 */

#include <xnix/console.h>
#include <xnix/font.h>
#include <xnix/types.h>
#include <xnix/utf8.h>

/* 外部 framebuffer 接口 */
extern bool     fb_available(void);
extern uint32_t fb_get_width(void);
extern uint32_t fb_get_height(void);
extern uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);
extern void     fb_putpixel(int x, int y, uint32_t color);
extern void     fb_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void     fb_scroll(int lines, int char_height, uint32_t bg_color);
extern void     fb_clear(uint32_t color);
extern void fb_draw_glyph(int px, int py, const uint8_t *glyph, int glyph_width, int glyph_height,
                          uint32_t fg, uint32_t bg);
extern void fb_init(void);
extern void fb_set_console_init_callback(void (*cb)(void));

/* 字符尺寸 */
#define CHAR_WIDTH  8
#define CHAR_HEIGHT 16

/* 控制台状态 */
static int      cursor_x = 0;
static int      cursor_y = 0;
static int      cols     = 0;
static int      rows     = 0;
static uint32_t fg_color = 0;
static uint32_t bg_color = 0;

/* 早期启动缓冲区(在 framebuffer 映射前保存日志) */
#define EARLY_BUFFER_SIZE 4096
static char     early_buffer[EARLY_BUFFER_SIZE];
static uint32_t early_buffer_pos    = 0;
static bool     early_buffer_active = true;

/* UTF-8 解码状态 */
static uint32_t utf8_state     = 0;
static uint32_t utf8_codepoint = 0;

/* ANSI 解析状态 */
enum ansi_state {
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
};

static enum ansi_state ansi_state       = ANSI_NORMAL;
static int             ansi_params[4]   = {0};
static int             ansi_param_count = 0;
static int             ansi_param_value = 0;

/* VGA 调色板(16色) */
static const struct {
    uint8_t r, g, b;
} vga_palette[16] = {
    {0x00, 0x00, 0x00}, /* 0: 黑 */
    {0x00, 0x00, 0xAA}, /* 1: 蓝 */
    {0x00, 0xAA, 0x00}, /* 2: 绿 */
    {0x00, 0xAA, 0xAA}, /* 3: 青 */
    {0xAA, 0x00, 0x00}, /* 4: 红 */
    {0xAA, 0x00, 0xAA}, /* 5: 品红 */
    {0xAA, 0x55, 0x00}, /* 6: 棕/暗黄 */
    {0xAA, 0xAA, 0xAA}, /* 7: 浅灰 */
    {0x55, 0x55, 0x55}, /* 8: 深灰 */
    {0x55, 0x55, 0xFF}, /* 9: 亮蓝 */
    {0x55, 0xFF, 0x55}, /* 10: 亮绿 */
    {0x55, 0xFF, 0xFF}, /* 11: 亮青 */
    {0xFF, 0x55, 0x55}, /* 12: 亮红 */
    {0xFF, 0x55, 0xFF}, /* 13: 亮品红 */
    {0xFF, 0xFF, 0x55}, /* 14: 黄 */
    {0xFF, 0xFF, 0xFF}, /* 15: 白 */
};

/* ANSI 颜色码到 VGA 索引 */
static const int ansi_to_vga[]        = {0, 4, 2, 6, 1, 5, 3, 7};
static const int ansi_to_vga_bright[] = {8, 12, 10, 14, 9, 13, 11, 15};

static uint32_t vga_to_rgb(int index) {
    if (index < 0 || index > 15) {
        index = 7;
    }
    return fb_rgb(vga_palette[index].r, vga_palette[index].g, vga_palette[index].b);
}

/**
 * 渲染一个字形
 */
static void fb_render_glyph(uint32_t codepoint, int px, int py) {
    int            glyph_width;
    const uint8_t *glyph = font_get_glyph(codepoint, &glyph_width);

    if (!glyph) {
        /* 未找到字形,使用替换字符 */
        glyph = font_get_glyph(0xFFFD, &glyph_width);
        if (!glyph) {
            /* 连替换字符都没有,用方块表示 */
            glyph = font_get_glyph('?', &glyph_width);
        }
    }

    if (!glyph) {
        return;
    }

    /* 使用优化的批量绘制函数 */
    fb_draw_glyph(px, py, glyph, glyph_width, CHAR_HEIGHT, fg_color, bg_color);
}

/**
 * 输出一个 Unicode 码点
 */
static void fb_console_put_codepoint(uint32_t cp) {
    int glyph_width;

    /* 获取字形宽度来决定占用多少列 */
    const uint8_t *glyph     = font_get_glyph(cp, &glyph_width);
    int            char_cols = (glyph_width > 8) ? 2 : 1;

    /* 检查是否需要换行 */
    if (cursor_x + char_cols > cols) {
        cursor_x = 0;
        cursor_y++;
    }

    /* 检查是否需要滚动 */
    if (cursor_y >= rows) {
        fb_scroll(1, CHAR_HEIGHT, bg_color);
        cursor_y = rows - 1;
    }

    /* 渲染字形 */
    int px = cursor_x * CHAR_WIDTH;
    int py = cursor_y * CHAR_HEIGHT;
    fb_render_glyph(cp, px, py);

    /* 移动光标 */
    cursor_x += char_cols;

    (void)glyph;
}

/**
 * 处理 ANSI SGR 命令
 */
static void ansi_handle_sgr(void) {
    for (int i = 0; i < ansi_param_count; i++) {
        int p = ansi_params[i];
        if (p == 0) {
            /* 重置 */
            fg_color = vga_to_rgb(7); /* 浅灰 */
            bg_color = vga_to_rgb(0); /* 黑 */
        } else if (p == 1) {
            /* 粗体/亮色 - 暂不支持 */
        } else if (p >= 30 && p <= 37) {
            fg_color = vga_to_rgb(ansi_to_vga[p - 30]);
        } else if (p >= 40 && p <= 47) {
            bg_color = vga_to_rgb(ansi_to_vga[p - 40]);
        } else if (p >= 90 && p <= 97) {
            fg_color = vga_to_rgb(ansi_to_vga_bright[p - 90]);
        } else if (p >= 100 && p <= 107) {
            bg_color = vga_to_rgb(ansi_to_vga_bright[p - 100]);
        }
    }

    if (ansi_param_count == 0) {
        fg_color = vga_to_rgb(7);
        bg_color = vga_to_rgb(0);
    }
}

/**
 * 处理 ANSI CSI 序列
 */
static void ansi_handle_csi(char cmd) {
    if (ansi_param_count < 4) {
        ansi_params[ansi_param_count++] = ansi_param_value;
    }

    switch (cmd) {
    case 'm':
        ansi_handle_sgr();
        break;
    case 'H':
    case 'f':
        /* 光标定位 */
        cursor_y = (ansi_param_count > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
        cursor_x = (ansi_param_count > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
        if (cursor_y >= rows) {
            cursor_y = rows - 1;
        }
        if (cursor_x >= cols) {
            cursor_x = cols - 1;
        }
        break;
    case 'J':
        /* 清屏 */
        if (ansi_params[0] == 2) {
            fb_clear(bg_color);
            cursor_x = 0;
            cursor_y = 0;
        }
        break;
    case 'K':
        /* 清除行 */
        if (ansi_params[0] == 0) {
            /* 清除从光标到行尾 */
            fb_fill_rect(cursor_x * CHAR_WIDTH, cursor_y * CHAR_HEIGHT,
                         (cols - cursor_x) * CHAR_WIDTH, CHAR_HEIGHT, bg_color);
        }
        break;
    default:
        break;
    }
}

/**
 * 处理一个字符(可能是 UTF-8 的一部分或控制字符)
 */
static void fb_console_putc_internal(char c) {
    uint8_t byte = (uint8_t)c;

    /* ANSI 序列解析优先 */
    switch (ansi_state) {
    case ANSI_ESC:
        if (c == '[') {
            ansi_state       = ANSI_CSI;
            ansi_param_count = 0;
            ansi_param_value = 0;
        } else {
            ansi_state = ANSI_NORMAL;
        }
        return;

    case ANSI_CSI:
        if (c >= '0' && c <= '9') {
            ansi_param_value = ansi_param_value * 10 + (c - '0');
        } else if (c == ';') {
            if (ansi_param_count < 4) {
                ansi_params[ansi_param_count++] = ansi_param_value;
            }
            ansi_param_value = 0;
        } else if (c >= 0x40 && c <= 0x7E) {
            ansi_handle_csi(c);
            ansi_state = ANSI_NORMAL;
        } else {
            ansi_state = ANSI_NORMAL;
        }
        return;

    case ANSI_NORMAL:
        if (c == '\x1b') {
            ansi_state = ANSI_ESC;
            return;
        }
        break;
    }

    /* 处理控制字符 */
    if (byte < 0x20) {
        switch (c) {
        case '\n':
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= rows) {
                fb_scroll(1, CHAR_HEIGHT, bg_color);
                cursor_y = rows - 1;
            }
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            cursor_x = (cursor_x + 8) & ~7;
            if (cursor_x >= cols) {
                cursor_x = 0;
                cursor_y++;
                if (cursor_y >= rows) {
                    fb_scroll(1, CHAR_HEIGHT, bg_color);
                    cursor_y = rows - 1;
                }
            }
            break;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                /* 用背景色清除当前位置 */
                fb_fill_rect(cursor_x * CHAR_WIDTH, cursor_y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT,
                             bg_color);
            }
            break;
        }
        return;
    }

    /* UTF-8 解码 */
    int result = utf8_decode_byte(&utf8_state, &utf8_codepoint, byte);
    if (result > 0) {
        /* 完整的码点 */
        fb_console_put_codepoint(utf8_codepoint);
    } else if (result < 0) {
        /* 解码错误,输出替换字符 */
        fb_console_put_codepoint(0xFFFD);
    }
    /* result == 0 表示需要更多字节,继续等待 */
}

/**
 * 延迟初始化(fb_late_init 调用后)
 */
static void fb_console_late_init(void) {
    if (!fb_available()) {
        return;
    }

    cols = fb_get_width() / CHAR_WIDTH;
    rows = fb_get_height() / CHAR_HEIGHT;

    fg_color = vga_to_rgb(7); /* 浅灰 */
    bg_color = vga_to_rgb(0); /* 黑 */

    fb_clear(bg_color);

    /* 回放早期缓冲区 */
    if (early_buffer_active && early_buffer_pos > 0) {
        for (uint32_t i = 0; i < early_buffer_pos; i++) {
            fb_console_putc_internal(early_buffer[i]);
        }
    }
    early_buffer_active = false;
}

static void fb_console_init(void) {
    /* 注册延迟初始化回调,fb_late_init 会在 VMM 就绪后调用 */
    fb_set_console_init_callback(fb_console_late_init);
}

static void fb_console_putc(char c) {
    if (!fb_available()) {
        /* framebuffer 未就绪,保存到早期缓冲区 */
        if (early_buffer_active && early_buffer_pos < EARLY_BUFFER_SIZE) {
            early_buffer[early_buffer_pos++] = c;
        }
        return;
    }
    fb_console_putc_internal(c);
}

static void fb_console_puts(const char *s) {
    if (!fb_available()) {
        return;
    }
    while (*s) {
        fb_console_putc_internal(*s++);
    }
}

static void fb_console_clear(void) {
    if (!fb_available()) {
        return;
    }
    fb_clear(bg_color);
    cursor_x = 0;
    cursor_y = 0;
}

static struct console fb_console = {
    .name        = "fb",
    .flags       = CONSOLE_SYNC,
    .init        = fb_console_init,
    .putc        = fb_console_putc,
    .puts        = fb_console_puts,
    .set_color   = NULL, /* 颜色通过 ANSI 序列处理 */
    .reset_color = NULL,
    .clear       = fb_console_clear,
};

void fb_console_register(void) {
    console_register(&fb_console);
}
