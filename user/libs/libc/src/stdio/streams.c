/**
 * @file streams.c
 * @brief FILE 流实现 (自适应通道)
 *
 * 管理 stdin/stdout/stderr 三个标准流.
 *
 * 通道机制:
 *   - CH_DEBUG: fd 无效时, 写入走 SYS_DEBUG_WRITE, 读取走 SYS_DEBUG_READ
 *   - CH_FD:    fd 有效时, 走标准 write()/read() -> IPC
 *   - 升级:     每 32 次 flush 尝试 reprobe fd, 单向 DEBUG -> FD
 */

#include <stdio_internal.h>
#include <string.h>
#include <unistd.h>
#include <xnix/fd.h>

/* reprobe 频率: 每 32 次 flush 尝试一次 */
#define REPROBE_INTERVAL 32

FILE _stdin_file;
FILE _stdout_file;
FILE _stderr_file;

/**
 * 根据 fd 有效性确定初始通道
 */
static enum stdio_channel _detect_channel(int fd) {
    if (fd >= 0 && fd_get(fd) != NULL) {
        return STDIO_CH_FD;
    }
    return STDIO_CH_DEBUG;
}

/**
 * 尝试升级通道: DEBUG -> FD
 *
 * 仅当 fd 已被外部安装(如 init handle 注入)时生效.
 * 通过 reprobe_cnt 限制调用频率.
 */
static void _stdio_maybe_upgrade(FILE *f) {
    if (f->channel == STDIO_CH_FD) {
        return; /* 已是最佳通道 */
    }

    f->reprobe_cnt++;
    if (f->reprobe_cnt < REPROBE_INTERVAL) {
        return;
    }
    f->reprobe_cnt = 0;

    if (f->fd >= 0 && fd_get(f->fd) != NULL) {
        f->channel = STDIO_CH_FD;
    }
}

void _libc_stdio_init(void) {
    memset(&_stdin_file, 0, sizeof(_stdin_file));
    _stdin_file.fd       = STDIN_FILENO;
    _stdin_file.buf_mode = _IONBF;
    _stdin_file.flags    = _FILE_READ;
    _stdin_file.channel  = _detect_channel(STDIN_FILENO);

    memset(&_stdout_file, 0, sizeof(_stdout_file));
    _stdout_file.fd       = STDOUT_FILENO;
    _stdout_file.buf_mode = _IOLBF;
    _stdout_file.flags    = _FILE_WRITE;
    _stdout_file.channel  = _detect_channel(STDOUT_FILENO);

    memset(&_stderr_file, 0, sizeof(_stderr_file));
    _stderr_file.fd       = STDERR_FILENO;
    _stderr_file.buf_mode = _IONBF;
    _stderr_file.flags    = _FILE_WRITE;
    _stderr_file.channel  = _detect_channel(STDERR_FILENO);
}

void _stdio_set_fd(FILE *f, int fd) {
    if (f) {
        f->fd = fd;
        /* fd 变化时重新检测通道 */
        f->channel = _detect_channel(fd);
    }
}

int _file_flush(FILE *f) {
    if (!f || f->buf_pos <= 0) {
        return 0;
    }

    _stdio_maybe_upgrade(f);

    switch (f->channel) {
    case STDIO_CH_DEBUG:
        _debug_write(f->buf, f->buf_pos);
        break;
    case STDIO_CH_FD:
        write(f->fd, f->buf, f->buf_pos);
        break;
    case STDIO_CH_NONE:
        /* 丢弃 */
        break;
    }

    f->buf_pos = 0;
    return 0;
}

int _file_putc(FILE *f, char c) {
    if (!f || !(f->flags & _FILE_WRITE)) {
        return EOF;
    }

    if (f->buf_mode == _IONBF) {
        /* 无缓冲: 立即发送 */
        _stdio_maybe_upgrade(f);

        switch (f->channel) {
        case STDIO_CH_DEBUG: {
            char ch = (char)c;
            _debug_write(&ch, 1);
            break;
        }
        case STDIO_CH_FD:
            write(f->fd, &c, 1);
            break;
        case STDIO_CH_NONE:
            break;
        }
        return (unsigned char)c;
    }

    f->buf[f->buf_pos++] = c;

    if ((f->buf_mode == _IOLBF && c == '\n') || f->buf_pos >= STREAM_BUF_SIZE - 1) {
        _file_flush(f);
    }

    return (unsigned char)c;
}

int _file_getc(FILE *f) {
    if (!f || !(f->flags & _FILE_READ)) {
        return EOF;
    }

    _stdio_maybe_upgrade(f);

    if (f->channel == STDIO_CH_DEBUG) {
        /* DEBUG 通道: 通过 SYS_DEBUG_READ 阻塞读 COM1 */
        char c = 0;
        int  n = _debug_read(&c, 1);
        if (n <= 0) {
            f->eof = 1;
            return EOF;
        }
        return (unsigned char)c;
    }

    if (f->channel != STDIO_CH_FD) {
        f->eof = 1;
        return EOF;
    }

    char    c = 0;
    ssize_t n = read(f->fd, &c, 1);
    if (n < 0) {
        f->error = 1;
        return EOF;
    }
    if (n == 0) {
        f->eof = 1;
        return EOF;
    }

    return (unsigned char)c;
}
