#ifndef XNIX_USER_UTF8_H
#define XNIX_USER_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t utf8_decode_next(const uint8_t *s, size_t len, uint32_t *codepoint);

#endif
