/**
 * @file kerr.c
 * @brief 内核错误码可读字符串实现
 *
 * 精简版,仅收录内核常用的错误码.
 * 表在 .rodata 段,不占堆内存,freestanding 安全.
 */

#include <xnix/errno.h>
#include <xnix/kerr.h>

static const char *kerr_messages[] = {
    [EOK]         = "success",
    [EPERM]       = "operation not permitted",
    [ENOENT]      = "no such file or directory",
    [ESRCH]       = "no such process",
    [EINTR]       = "interrupted",
    [EIO]         = "I/O error",
    [ENXIO]       = "no such device or address",
    [E2BIG]       = "argument list too long",
    [ENOEXEC]     = "exec format error",
    [EBADF]       = "bad file descriptor",
    [ECHILD]      = "no child processes",
    [EAGAIN]      = "resource temporarily unavailable",
    [ENOMEM]      = "out of memory",
    [EACCES]      = "permission denied",
    [EFAULT]      = "bad address",
    [EBUSY]       = "device busy",
    [EEXIST]      = "file exists",
    [ENODEV]      = "no such device",
    [ENOTDIR]     = "not a directory",
    [EISDIR]      = "is a directory",
    [EINVAL]      = "invalid argument",
    [ENFILE]      = "file table overflow",
    [EMFILE]      = "too many open files",
    [ENOSPC]      = "no space left on device",
    [EROFS]       = "read-only file system",
    [ENOSYS]      = "function not implemented",
    [ENOTEMPTY]   = "directory not empty",
    [ENOHANDLE]   = "invalid handle",
    [ENOPERM_NODE] = "permission node not found",
    [EENDPOINT]   = "invalid endpoint",
    [ETIMEOUT]    = "timed out",
};

#define KERR_TABLE_SIZE (sizeof(kerr_messages) / sizeof(kerr_messages[0]))

const char *kerr(int errnum) {
    if (errnum < 0) {
        errnum = -errnum;
    }
    if ((unsigned)errnum < KERR_TABLE_SIZE && kerr_messages[errnum]) {
        return kerr_messages[errnum];
    }
    return "unknown error";
}
