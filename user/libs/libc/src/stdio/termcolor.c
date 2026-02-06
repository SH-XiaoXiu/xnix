/**
 * @file termcolor.c
 * @brief 终端颜色控制实现
 */

#include <d/protocol/tty.h>
#include <stdio_internal.h>
#include <string.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>
#include <xnix/termcolor.h>

static handle_t termcolor_get_tty_ep(FILE *stream) {
    if (!stream) {
        return HANDLE_INVALID;
    }
    return stream->tty_ep;
}

int termcolor_set(FILE *stream, enum term_color fg, enum term_color bg) {
    if (!stream) {
        return -1;
    }

    handle_t tty_ep = termcolor_get_tty_ep(stream);
    if (tty_ep == HANDLE_INVALID) {
        return -1;
    }

    fflush(stream);

    struct ipc_message req;
    struct ipc_message reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));
    req.regs.data[0] = TTY_OP_IOCTL;
    req.regs.data[1] = TTY_IOCTL_SET_COLOR;
    req.regs.data[2] = (uint32_t)(fg & 0x0F);
    req.regs.data[3] = (uint32_t)(bg & 0x0F);

    if (sys_ipc_call(tty_ep, &req, &reply, 100) != 0) {
        return -1;
    }
    return (int)reply.regs.data[0];
}

int termcolor_reset(FILE *stream) {
    if (!stream) {
        return -1;
    }

    handle_t tty_ep = termcolor_get_tty_ep(stream);
    if (tty_ep == HANDLE_INVALID) {
        return -1;
    }

    fflush(stream);

    struct ipc_message req;
    struct ipc_message reply;
    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));
    req.regs.data[0] = TTY_OP_IOCTL;
    req.regs.data[1] = TTY_IOCTL_RESET_COLOR;

    if (sys_ipc_call(tty_ep, &req, &reply, 100) != 0) {
        return -1;
    }
    return (int)reply.regs.data[0];
}
