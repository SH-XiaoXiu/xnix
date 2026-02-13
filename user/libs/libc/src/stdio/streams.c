/**
 * @file streams.c
 * @brief FILE 流实现
 *
 * 管理 stdin/stdout/stderr 三个标准流,通过统一 fd 层进行 I/O.
 */

#include <stdio_internal.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/handle.h>
#include <xnix/fd.h>
#include <xnix/syscall.h>

FILE _stdin_file;
FILE _stdout_file;
FILE _stderr_file;

/**
 * SYS_DEBUG_WRITE fallback (ttyd 就绪前)
 */
static inline void _debug_write(const void *buf, size_t len) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(SYS_DEBUG_WRITE), "b"((uint32_t)(uintptr_t)buf), "c"((uint32_t)len)
                 : "memory");
    (void)ret;
}

void _libc_stdio_init(void) {
    /* fd 0/1/2 已由 fd_table_init() 建立,这里只设置 FILE 结构 */
    memset(&_stdin_file, 0, sizeof(_stdin_file));
    _stdin_file.fd       = STDIN_FILENO;
    _stdin_file.buf_mode = _IONBF;
    _stdin_file.flags    = _FILE_READ;

    memset(&_stdout_file, 0, sizeof(_stdout_file));
    _stdout_file.fd       = STDOUT_FILENO;
    _stdout_file.buf_mode = _IOLBF;
    _stdout_file.flags    = _FILE_WRITE;

    memset(&_stderr_file, 0, sizeof(_stderr_file));
    _stderr_file.fd       = STDERR_FILENO;
    _stderr_file.buf_mode = _IONBF;
    _stderr_file.flags    = _FILE_WRITE;
}

void _stdio_force_debug_mode(void) {
    _stdout_file.fd = -1;
    _stderr_file.fd = -1;
}

void _stdio_set_fd(FILE *f, int fd) {
    if (f) {
        f->fd = fd;
    }
}

int _file_flush(FILE *f) {
    if (!f || f->buf_pos <= 0) {
        return 0;
    }

    if (f->fd < 0) {
        /* fallback: SYS_DEBUG_WRITE */
        _debug_write(f->buf, f->buf_pos);
        f->buf_pos = 0;
        return 0;
    }

    write(f->fd, f->buf, f->buf_pos);
    f->buf_pos = 0;
    return 0;
}

int _file_putc(FILE *f, char c) {
    if (!f || !(f->flags & _FILE_WRITE)) {
        return EOF;
    }

    if (f->buf_mode == _IONBF) {
        /* 无缓冲:立即发送 */
        if (f->fd < 0) {
            char ch = (char)c;
            _debug_write(&ch, 1);
            return (unsigned char)c;
        }
        write(f->fd, &c, 1);
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

    for (;;) {
        if (f->fd < 0) {
            f->error = 1;
            msleep(10);
            continue;
        }

        char    c;
        ssize_t n = read(f->fd, &c, 1);
        if (n < 0) {
            msleep(10);
            continue;
        }
        if (n == 0) {
            msleep(1);
            continue;
        }

        return (unsigned char)c;
    }
}
