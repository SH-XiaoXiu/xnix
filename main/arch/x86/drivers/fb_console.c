/**
 * @file fb_console.c
 * @brief Framebuffer 控制台驱动 - 早期控制台后端
 *
 * 在 framebuffer 上渲染字符,支持 UTF-8 解码.
 * 纯文本输出,无 ANSI 转义解析,无颜色控制.
 * 固定前景色浅灰,背景色黑.
 */

#include <xnix/early_console.h>
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
static uint8_t  cur_fg   = EARLY_COLOR_LIGHT_GREY;
static uint8_t  cur_bg   = EARLY_COLOR_BLACK;

/* 早期启动缓冲区(在 framebuffer 映射前保存日志) */
#define EARLY_BUFFER_SIZE 4096
struct fb_early_cell {
    char    c;
    uint8_t fg;
    uint8_t bg;
};
static struct fb_early_cell early_buffer[EARLY_BUFFER_SIZE];
static uint32_t             early_buffer_pos    = 0;
static bool                 early_buffer_active = true;

/* UTF-8 解码状态 */
static uint32_t utf8_state     = 0;
static uint32_t utf8_codepoint = 0;

static uint32_t fb_console_color_to_rgb(uint8_t color) {
    switch (color & 0x0F) {
    case EARLY_COLOR_BLACK:
        return fb_rgb(0x00, 0x00, 0x00);
    case EARLY_COLOR_BLUE:
        return fb_rgb(0x00, 0x00, 0xAA);
    case EARLY_COLOR_GREEN:
        return fb_rgb(0x00, 0xAA, 0x00);
    case EARLY_COLOR_CYAN:
        return fb_rgb(0x00, 0xAA, 0xAA);
    case EARLY_COLOR_RED:
        return fb_rgb(0xAA, 0x00, 0x00);
    case EARLY_COLOR_MAGENTA:
        return fb_rgb(0xAA, 0x00, 0xAA);
    case EARLY_COLOR_BROWN:
        return fb_rgb(0xAA, 0x55, 0x00);
    case EARLY_COLOR_LIGHT_GREY:
        return fb_rgb(0xAA, 0xAA, 0xAA);
    case EARLY_COLOR_DARK_GREY:
        return fb_rgb(0x55, 0x55, 0x55);
    case EARLY_COLOR_LIGHT_BLUE:
        return fb_rgb(0x55, 0x55, 0xFF);
    case EARLY_COLOR_LIGHT_GREEN:
        return fb_rgb(0x55, 0xFF, 0x55);
    case EARLY_COLOR_LIGHT_CYAN:
        return fb_rgb(0x55, 0xFF, 0xFF);
    case EARLY_COLOR_LIGHT_RED:
        return fb_rgb(0xFF, 0x55, 0x55);
    case EARLY_COLOR_LIGHT_MAGENTA:
        return fb_rgb(0xFF, 0x55, 0xFF);
    case EARLY_COLOR_LIGHT_BROWN:
        return fb_rgb(0xFF, 0xFF, 0x55);
    case EARLY_COLOR_WHITE:
        return fb_rgb(0xFF, 0xFF, 0xFF);
    default:
        return fb_rgb(0xAA, 0xAA, 0xAA);
    }
}

static void fb_console_apply_color(uint8_t fg, uint8_t bg) {
    cur_fg = fg & 0x0F;
    cur_bg = bg & 0x0F;
    if (fb_available()) {
        fg_color = fb_console_color_to_rgb(cur_fg);
        bg_color = fb_console_color_to_rgb(cur_bg);
    }
}

/**
 * 渲染一个字形
 */
static void fb_render_glyph(uint32_t codepoint, int px, int py) {
    int            glyph_width;
    const uint8_t *glyph = font_get_glyph(codepoint, &glyph_width);

    if (!glyph) {
        glyph = font_get_glyph(0xFFFD, &glyph_width);
        if (!glyph) {
            glyph = font_get_glyph('?', &glyph_width);
        }
    }

    if (!glyph) {
        return;
    }

    fb_draw_glyph(px, py, glyph, glyph_width, CHAR_HEIGHT, fg_color, bg_color);
}

/**
 * 输出一个 Unicode 码点
 */
static void fb_console_put_codepoint(uint32_t cp) {
    int glyph_width;

    const uint8_t *glyph     = font_get_glyph(cp, &glyph_width);
    int            char_cols = (glyph_width > 8) ? 2 : 1;

    if (cursor_x + char_cols > cols) {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= rows) {
        fb_scroll(1, CHAR_HEIGHT, bg_color);
        cursor_y = rows - 1;
    }

    int px = cursor_x * CHAR_WIDTH;
    int py = cursor_y * CHAR_HEIGHT;
    fb_render_glyph(cp, px, py);

    cursor_x += char_cols;

    (void)glyph;
}

/**
 * 处理一个字符(可能是 UTF-8 的一部分或控制字符)
 */
static void fb_console_putc_internal(char c) {
    uint8_t byte = (uint8_t)c;

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
        fb_console_put_codepoint(utf8_codepoint);
    } else if (result < 0) {
        fb_console_put_codepoint(0xFFFD);
    }
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

    fb_console_apply_color(EARLY_COLOR_LIGHT_GREY, EARLY_COLOR_BLACK);

    fb_clear(bg_color);

    /* 回放早期缓冲区 */
    if (early_buffer_active && early_buffer_pos > 0) {
        uint8_t last_fg = cur_fg;
        uint8_t last_bg = cur_bg;
        for (uint32_t i = 0; i < early_buffer_pos; i++) {
            if (early_buffer[i].fg != last_fg || early_buffer[i].bg != last_bg) {
                fb_console_apply_color(early_buffer[i].fg, early_buffer[i].bg);
                last_fg = early_buffer[i].fg;
                last_bg = early_buffer[i].bg;
            }
            fb_console_putc_internal(early_buffer[i].c);
        }
    }
    early_buffer_active = false;
}

static void fb_console_init(void) {
    fb_set_console_init_callback(fb_console_late_init);
}

static void fb_console_putc(char c) {
    if (!fb_available()) {
        if (early_buffer_active && early_buffer_pos < EARLY_BUFFER_SIZE) {
            early_buffer[early_buffer_pos++] = (struct fb_early_cell){
                .c  = c,
                .fg = cur_fg,
                .bg = cur_bg,
            };
        }
        return;
    }
    fb_console_putc_internal(c);
}

static void fb_console_puts(const char *s) {
    if (!s) {
        return;
    }
    while (*s) {
        fb_console_putc(*s++);
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

static void fb_console_set_color(uint8_t fg, uint8_t bg) {
    fb_console_apply_color(fg, bg);
}

static void fb_console_reset_color(void) {
    fb_console_apply_color(EARLY_COLOR_LIGHT_GREY, EARLY_COLOR_BLACK);
}

static struct early_console_backend fb_backend = {
    .name        = "fb",
    .init        = fb_console_init,
    .putc        = fb_console_putc,
    .puts        = fb_console_puts,
    .clear       = fb_console_clear,
    .set_color   = fb_console_set_color,
    .reset_color = fb_console_reset_color,
};

void fb_console_register(void) {
    early_console_register(&fb_backend);
}
