#include <string.h>

size_t strnlen(const char *s, size_t max_len) {
    size_t n = 0;
    while (n < max_len && s[n]) {
        n++;
    }
    return n;
}
