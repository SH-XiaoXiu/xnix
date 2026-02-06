/**
 * @file streams.c
 * @brief FILE 流实现
 *
 * 管理 stdin/stdout/stderr 三个标准流,通过 TTY IPC 协议与 ttyd 通信.
 */

#include <stdio_internal.h>
#include <string.h>
#include <d/protocol/tty.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

FILE _stdin_file;
FILE _stdout_file;
FILE _stderr_file;

/* 查找 tty endpoint */
static handle_t find_tty_ep(void) {
    handle_t h;

    h = env_get_handle("tty1");
    if (h != HANDLE_INVALID) return h;

    h = env_get_handle("tty0");
    if (h != HANDLE_INVALID) return h;

    return HANDLE_INVALID;
}

void _libc_stdio_init(void) {
    handle_t tty = find_tty_ep();

    memset(&_stdin_file, 0, sizeof(_stdin_file));
    _stdin_file.tty_ep   = tty;
    _stdin_file.buf_mode = _IONBF;
    _stdin_file.flags    = _FILE_READ;

    memset(&_stdout_file, 0, sizeof(_stdout_file));
    _stdout_file.tty_ep   = tty;
    _stdout_file.buf_mode = _IOLBF;
    _stdout_file.flags    = _FILE_WRITE;

    memset(&_stderr_file, 0, sizeof(_stderr_file));
    _stderr_file.tty_ep   = tty;
    _stderr_file.buf_mode = _IONBF;
    _stderr_file.flags    = _FILE_WRITE;
}

int _file_flush(FILE *f) {
    if (!f || f->buf_pos <= 0) {
        return 0;
    }

    if (f->tty_ep == HANDLE_INVALID) {
        f->buf_pos = 0;
        return -1;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = TTY_OP_WRITE;
    msg.regs.data[1] = (uint32_t)f->buf_pos;
    msg.buffer.data   = f->buf;
    msg.buffer.size   = (uint32_t)f->buf_pos;

    sys_ipc_send(f->tty_ep, &msg, 100);
    f->buf_pos = 0;
    return 0;
}

int _file_putc(FILE *f, char c) {
    if (!f || !(f->flags & _FILE_WRITE)) {
        return EOF;
    }

    if (f->buf_mode == _IONBF) {
        /* 无缓冲:立即发送 */
        if (f->tty_ep == HANDLE_INVALID) {
            return EOF;
        }
        struct ipc_message msg;
        memset(&msg, 0, sizeof(msg));
        msg.regs.data[0] = TTY_OP_PUTC;
        msg.regs.data[1] = (uint32_t)(unsigned char)c;
        sys_ipc_send(f->tty_ep, &msg, 100);
        return (unsigned char)c;
    }

    f->buf[f->buf_pos++] = c;

    if (f->buf_mode == _IOLBF && c == '\n') {
        _file_flush(f);
    } else if (f->buf_pos >= STREAM_BUF_SIZE - 1) {
        _file_flush(f);
    }

    return (unsigned char)c;
}

int _file_getc(FILE *f) {
    if (!f || !(f->flags & _FILE_READ)) {
        return EOF;
    }
    if (f->eof) {
        return EOF;
    }
    if (f->tty_ep == HANDLE_INVALID) {
        return EOF;
    }

    struct ipc_message req;
    struct ipc_message reply;
    char               recv_buf[4];

    memset(&req, 0, sizeof(req));
    memset(&reply, 0, sizeof(reply));

    req.regs.data[0] = TTY_OP_READ;
    req.regs.data[1] = 1;

    reply.buffer.data = recv_buf;
    reply.buffer.size = sizeof(recv_buf);

    int ret = sys_ipc_call(f->tty_ep, &req, &reply, 0);
    if (ret != 0) {
        f->error = 1;
        return EOF;
    }

    int n = (int)reply.regs.data[0];
    if (n <= 0) {
        f->eof = 1;
        return EOF;
    }

    return (unsigned char)recv_buf[0];
}
