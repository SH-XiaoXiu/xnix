/**
 * @file font.c
 * @brief 字体管理
 *
 * 支持内嵌 ASCII 字体和外部加载的 CJK 字体.
 */

#include <xnix/font.h>
#include <xnix/string.h>

/* 内嵌 ASCII 字体(8x16) */
extern const uint8_t  font_ascii_8x16[][16];
extern const uint32_t font_ascii_count;

/* CJK 字体数据(运行时加载) */
static const uint8_t  *cjk_glyphs = NULL;
static const uint32_t *cjk_index  = NULL;
static uint32_t        cjk_count  = 0;
static uint8_t         cjk_width  = 16;
static uint8_t         cjk_bpg    = 32; /* bytes per glyph */

/* 替换字符(U+FFFD)的字形 */
static const uint8_t replacement_glyph[32] = {
    0x00, 0x00, 0x00, 0x00, 0x07, 0xE0, 0x0C, 0x30, 0x10, 0x08, 0x20, 0x04, 0x23, 0xC4, 0x26, 0x64,
    0x26, 0x64, 0x23, 0xC4, 0x20, 0x04, 0x10, 0x08, 0x0C, 0x30, 0x07, 0xE0, 0x00, 0x00, 0x00, 0x00,
};

void font_init(void) {
    /* ASCII 字体是编译时内嵌的,无需初始化 */
    /* CJK 字体需要调用 font_load_cjk 加载 */
}

int font_load_cjk(const void *data, uint32_t size) {
    if (!data || size < sizeof(struct font_file_header)) {
        return -1;
    }

    const struct font_file_header *header = (const struct font_file_header *)data;

    if (header->magic != FONT_MAGIC) {
        return -2;
    }

    if (header->version != 1) {
        return -3;
    }

    /* 计算所需大小 */
    uint32_t index_size  = header->glyph_count * sizeof(uint32_t);
    uint32_t glyphs_size = header->glyph_count * header->bytes_per_glyph;
    uint32_t total_size  = sizeof(struct font_file_header) + index_size + glyphs_size;

    if (size < total_size) {
        return -4;
    }

    /* 设置指针 */
    cjk_index  = (const uint32_t *)((const uint8_t *)data + sizeof(struct font_file_header));
    cjk_glyphs = (const uint8_t *)data + sizeof(struct font_file_header) + index_size;
    cjk_count  = header->glyph_count;
    cjk_width  = header->glyph_width;
    cjk_bpg    = header->bytes_per_glyph;

    return 0;
}

bool font_cjk_loaded(void) {
    return cjk_glyphs != NULL;
}

/**
 * 二分查找 CJK 字形
 */
static const uint8_t *cjk_find_glyph(uint32_t codepoint) {
    if (!cjk_glyphs || cjk_count == 0) {
        return NULL;
    }

    uint32_t low = 0, high = cjk_count;

    while (low < high) {
        uint32_t mid = (low + high) / 2;
        if (cjk_index[mid] < codepoint) {
            low = mid + 1;
        } else if (cjk_index[mid] > codepoint) {
            high = mid;
        } else {
            return cjk_glyphs + mid * cjk_bpg;
        }
    }

    return NULL;
}

const uint8_t *font_get_glyph(uint32_t codepoint, int *width) {
    /* ASCII 范围 */
    if (codepoint < font_ascii_count) {
        if (width) {
            *width = 8;
        }
        /* 检查是否有有效字形(非零) */
        const uint8_t *glyph = font_ascii_8x16[codepoint];
        /* 空格等字符是全零的,也是有效的 */
        if (codepoint >= 0x20 && codepoint < 0x7F) {
            return glyph;
        }
        /* 对于控制字符,返回 NULL 让调用者处理 */
        if (codepoint < 0x20) {
            return NULL;
        }
        return glyph;
    }

    /* 替换字符 */
    if (codepoint == 0xFFFD) {
        if (width) {
            *width = 16;
        }
        return replacement_glyph;
    }

    /* CJK 范围查找 */
    const uint8_t *glyph = cjk_find_glyph(codepoint);
    if (glyph) {
        if (width) {
            *width = cjk_width;
        }
        return glyph;
    }

    /* 未找到 */
    if (width) {
        *width = 0;
    }
    return NULL;
}
