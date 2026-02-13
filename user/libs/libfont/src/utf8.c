#include <font/utf8.h>

size_t utf8_decode_next(const uint8_t *s, size_t len, uint32_t *codepoint) {
    if (!s || len == 0 || !codepoint) {
        return 0;
    }

    uint8_t b0 = s[0];

    if ((b0 & 0x80u) == 0) {
        *codepoint = (uint32_t)b0;
        return 1;
    }

    if ((b0 & 0xE0u) == 0xC0u) {
        if (len < 2) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        uint8_t b1 = s[1];
        if ((b1 & 0xC0u) != 0x80u) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)(b1 & 0x3Fu);
        if (cp < 0x80u) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        *codepoint = cp;
        return 2;
    }

    if ((b0 & 0xF0u) == 0xE0u) {
        if (len < 3) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        uint8_t b1 = s[1];
        uint8_t b2 = s[2];
        if (((b1 & 0xC0u) != 0x80u) || ((b2 & 0xC0u) != 0x80u)) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        uint32_t cp =
            ((uint32_t)(b0 & 0x0Fu) << 12) | ((uint32_t)(b1 & 0x3Fu) << 6) | (uint32_t)(b2 & 0x3Fu);
        if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        *codepoint = cp;
        return 3;
    }

    if ((b0 & 0xF8u) == 0xF0u) {
        if (len < 4) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        uint8_t b1 = s[1];
        uint8_t b2 = s[2];
        uint8_t b3 = s[3];
        if (((b1 & 0xC0u) != 0x80u) || ((b2 & 0xC0u) != 0x80u) || ((b3 & 0xC0u) != 0x80u)) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x07u) << 18) | ((uint32_t)(b1 & 0x3Fu) << 12) |
                      ((uint32_t)(b2 & 0x3Fu) << 6) | (uint32_t)(b3 & 0x3Fu);
        if (cp < 0x10000u || cp > 0x10FFFFu) {
            *codepoint = 0xFFFDu;
            return 1;
        }
        *codepoint = cp;
        return 4;
    }

    *codepoint = 0xFFFDu;
    return 1;
}
