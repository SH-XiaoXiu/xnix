#ifndef XNIX_FONT_H
#define XNIX_FONT_H

#include <xnix/types.h>

/**
 * 字体文件头(二进制格式)
 */
struct font_file_header {
    uint32_t magic;           /* "XFNT" = FONT_MAGIC */
    uint16_t version;         /* 版本号 */
    uint16_t glyph_count;     /* 字形数量 */
    uint8_t  glyph_width;     /* 字形宽度(8 或 16) */
    uint8_t  glyph_height;    /* 字形高度(通常 16) */
    uint8_t  bytes_per_glyph; /* 每个字形的字节数 */
    uint8_t  reserved;
};

#define FONT_MAGIC 0x544E4658 /* "XFNT" little-endian */

/**
 * 初始化字体系统
 *
 * 内嵌 ASCII 字体会自动可用,CJK 字体需要调用 font_load_cjk 加载.
 */
void font_init(void);

/**
 * 加载 CJK 字体
 *
 * @param data 字体数据(font_file_header + 索引 + 字形)
 * @param size 数据大小
 * @return 0 成功,<0 失败
 */
int font_load_cjk(const void *data, uint32_t size);

/**
 * 获取指定码点的字形
 *
 * @param codepoint Unicode 码点
 * @param width     输出参数:字形宽度(8 或 16)
 * @return 字形位图数据,NULL 表示未找到
 */
const uint8_t *font_get_glyph(uint32_t codepoint, int *width);

/**
 * 检查 CJK 字体是否已加载
 */
bool font_cjk_loaded(void);

#endif
