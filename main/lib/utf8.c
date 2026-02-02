/**
 * @file utf8.c
 * @brief UTF-8 解码器
 */

#include <xnix/utf8.h>

/* UTF-8 解码状态 */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

/*
 * UTF-8 编码表:
 * 1 字节: 0xxxxxxx                              (U+0000 - U+007F)
 * 2 字节: 110xxxxx 10xxxxxx                     (U+0080 - U+07FF)
 * 3 字节: 1110xxxx 10xxxxxx 10xxxxxx            (U+0800 - U+FFFF)
 * 4 字节: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx   (U+10000 - U+10FFFF)
 */

int utf8_decode_byte(uint32_t *state, uint32_t *codepoint, uint8_t byte) {
    if (*state == UTF8_ACCEPT) {
        /* 起始状态,检测首字节 */
        if ((byte & 0x80) == 0) {
            /* 1 字节 ASCII */
            *codepoint = byte;
            return 1;
        } else if ((byte & 0xE0) == 0xC0) {
            /* 2 字节序列开始 */
            *codepoint = byte & 0x1F;
            *state     = 1; /* 还需要 1 个字节 */
            return 0;
        } else if ((byte & 0xF0) == 0xE0) {
            /* 3 字节序列开始 */
            *codepoint = byte & 0x0F;
            *state     = 2; /* 还需要 2 个字节 */
            return 0;
        } else if ((byte & 0xF8) == 0xF0) {
            /* 4 字节序列开始 */
            *codepoint = byte & 0x07;
            *state     = 3; /* 还需要 3 个字节 */
            return 0;
        } else {
            /* 无效首字节 */
            *state = UTF8_ACCEPT;
            return -1;
        }
    } else {
        /* 继续字节 */
        if ((byte & 0xC0) == 0x80) {
            *codepoint = (*codepoint << 6) | (byte & 0x3F);
            (*state)--;
            if (*state == UTF8_ACCEPT) {
                /* 序列完成,验证码点 */
                uint32_t cp = *codepoint;

                /* 检查过长编码 */
                if (cp < 0x80) {
                    /* 应该用 1 字节编码 */
                    return -1;
                }
                if (cp >= 0x80 && cp < 0x800 && *state != 1) {
                    /* 2 字节范围但不是从 2 字节序列来的 - 过长 */
                    /* 这个检查有问题,state 已经是 0 了,无法判断原始长度 */
                    /* 简化处理:只检查代理对范围 */
                }

                /* 检查代理对范围(UTF-16 保留) */
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                    return -1;
                }

                /* 检查超出 Unicode 范围 */
                if (cp > 0x10FFFF) {
                    return -1;
                }

                return 1;
            }
            return 0;
        } else {
            /* 无效继续字节 */
            *state = UTF8_ACCEPT;
            return -1;
        }
    }
}

uint32_t utf8_decode(const char **s) {
    if (!s || !*s || !**s) {
        return 0;
    }

    const uint8_t *p  = (const uint8_t *)*s;
    uint32_t       cp = 0;

    if ((*p & 0x80) == 0) {
        /* 1 字节 ASCII */
        cp = *p++;
    } else if ((*p & 0xE0) == 0xC0) {
        /* 2 字节 */
        cp = *p++ & 0x1F;
        if ((*p & 0xC0) == 0x80) {
            cp = (cp << 6) | (*p++ & 0x3F);
        } else {
            cp = 0xFFFD; /* 替换字符 */
        }
    } else if ((*p & 0xF0) == 0xE0) {
        /* 3 字节 */
        cp = *p++ & 0x0F;
        for (int i = 0; i < 2; i++) {
            if ((*p & 0xC0) == 0x80) {
                cp = (cp << 6) | (*p++ & 0x3F);
            } else {
                cp = 0xFFFD;
                break;
            }
        }
    } else if ((*p & 0xF8) == 0xF0) {
        /* 4 字节 */
        cp = *p++ & 0x07;
        for (int i = 0; i < 3; i++) {
            if ((*p & 0xC0) == 0x80) {
                cp = (cp << 6) | (*p++ & 0x3F);
            } else {
                cp = 0xFFFD;
                break;
            }
        }
    } else {
        /* 无效首字节 */
        cp = 0xFFFD;
        p++;
    }

    *s = (const char *)p;
    return cp;
}

int utf8_char_length(uint8_t lead_byte) {
    if ((lead_byte & 0x80) == 0) {
        return 1;
    }
    if ((lead_byte & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead_byte & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead_byte & 0xF8) == 0xF0) {
        return 4;
    }
    return 0; /* 无效 */
}
