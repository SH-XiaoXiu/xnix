/**
 * @file termcolor.c
 * @brief 终端颜色控制实现
 */

#include <xnix/protocol/tty.h>
#include <sys/ioctl.h>
#include <stdio_internal.h>
#include <xnix/termcolor.h>

int termcolor_set(FILE *stream, enum term_color fg, enum term_color bg) {
    if (!stream || stream->fd < 0) {
        return -1;
    }

    fflush(stream);
    return ioctl(stream->fd, TTY_IOCTL_SET_COLOR, (unsigned long)(fg & 0x0F),
                 (unsigned long)(bg & 0x0F));
}

int termcolor_reset(FILE *stream) {
    if (!stream || stream->fd < 0) {
        return -1;
    }

    fflush(stream);
    return ioctl(stream->fd, TTY_IOCTL_RESET_COLOR);
}
