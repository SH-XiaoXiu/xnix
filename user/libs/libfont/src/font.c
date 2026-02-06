#include "stddef.h"

#include <font/font.h>
#include <font/font_ascii_8x16.h>

const uint8_t *font_get_ascii_8x16(uint32_t codepoint) {
    if (codepoint < 0x20u || codepoint == 0x7Fu) {
        return NULL;
    }
    if (codepoint < font_ascii_count) {
        return font_ascii_8x16[codepoint];
    }
    return font_ascii_8x16[(uint32_t)'?'];
}
