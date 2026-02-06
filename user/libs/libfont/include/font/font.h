#ifndef XNIX_USER_FONT_H
#define XNIX_USER_FONT_H

#include <stdint.h>

#define FONT_ASCII_WIDTH  8u
#define FONT_ASCII_HEIGHT 16u

const uint8_t *font_get_ascii_8x16(uint32_t codepoint);

#endif
