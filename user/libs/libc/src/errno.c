/**
 * @file errno.c
 * @brief errno 支持实现
 */

#include <errno.h>
#include <stdio.h>

/**
 * 全局错误码变量
 * TODO: 实现 TLS 支持后改为线程局部变量 (__thread)
 */
int errno = 0;

/**
 * 错误消息字符串表
 */
static const char *error_messages[] = {
    [XNIX_EOK]             = "Success",
    [XNIX_EPERM]           = "Operation not permitted",
    [XNIX_ENOENT]          = "No such file or directory",
    [XNIX_ESRCH]           = "No such process",
    [XNIX_EINTR]           = "Interrupted system call",
    [XNIX_EIO]             = "I/O error",
    [XNIX_ENXIO]           = "No such device or address",
    [XNIX_E2BIG]           = "Argument list too long",
    [XNIX_ENOEXEC]         = "Exec format error",
    [XNIX_EBADF]           = "Bad file descriptor",
    [XNIX_ECHILD]          = "No child processes",
    [XNIX_EAGAIN]          = "Resource temporarily unavailable",
    [XNIX_ENOMEM]          = "Out of memory",
    [XNIX_EACCES]          = "Permission denied",
    [XNIX_EFAULT]          = "Bad address",
    [XNIX_ENOTBLK]         = "Block device required",
    [XNIX_EBUSY]           = "Device or resource busy",
    [XNIX_EEXIST]          = "File exists",
    [XNIX_EXDEV]           = "Cross-device link",
    [XNIX_ENODEV]          = "No such device",
    [XNIX_ENOTDIR]         = "Not a directory",
    [XNIX_EISDIR]          = "Is a directory",
    [XNIX_EINVAL]          = "Invalid argument",
    [XNIX_ENFILE]          = "File table overflow",
    [XNIX_EMFILE]          = "Too many open files",
    [XNIX_ENOTTY]          = "Inappropriate ioctl for device",
    [XNIX_ETXTBSY]         = "Text file busy",
    [XNIX_EFBIG]           = "File too large",
    [XNIX_ENOSPC]          = "No space left on device",
    [XNIX_ESPIPE]          = "Illegal seek",
    [XNIX_EROFS]           = "Read-only file system",
    [XNIX_EMLINK]          = "Too many links",
    [XNIX_EPIPE]           = "Broken pipe",
    [XNIX_EDOM]            = "Math argument out of domain",
    [XNIX_ERANGE]          = "Math result not representable",
    [XNIX_EDEADLK]         = "Resource deadlock avoided",
    [XNIX_ENAMETOOLONG]    = "File name too long",
    [XNIX_ENOLCK]          = "No record locks available",
    [XNIX_ENOSYS]          = "Function not implemented",
    [XNIX_ENOTEMPTY]       = "Directory not empty",
    [XNIX_ELOOP]           = "Too many symbolic links",
    [XNIX_ENOMSG]          = "No message of desired type",
    [XNIX_EIDRM]           = "Identifier removed",
    [XNIX_ENODATA]         = "No data available",
    [XNIX_ENOSTR]          = "Device not a stream",
    [XNIX_EPROTO]          = "Protocol error",
    [XNIX_EBADMSG]         = "Bad message",
    [XNIX_EOVERFLOW]       = "Value too large for defined data type",
    [XNIX_EILSEQ]          = "Illegal byte sequence",
    [XNIX_ENOTSOCK]        = "Socket operation on non-socket",
    [XNIX_EDESTADDRREQ]    = "Destination address required",
    [XNIX_EMSGSIZE]        = "Message too long",
    [XNIX_EPROTOTYPE]      = "Protocol wrong type for socket",
    [XNIX_ENOPROTOOPT]     = "Protocol not available",
    [XNIX_EPROTONOSUPPORT] = "Protocol not supported",
    [XNIX_EAFNOSUPPORT]    = "Address family not supported",
    [XNIX_EADDRINUSE]      = "Address already in use",
    [XNIX_EADDRNOTAVAIL]   = "Cannot assign requested address",
    [XNIX_ENETDOWN]        = "Network is down",
    [XNIX_ENOTCONN]        = "Transport endpoint not connected",
    [XNIX_ESHUTDOWN]       = "Endpoint has been shut down",
    [XNIX_ENOHANDLE]       = "Invalid handle",
    [XNIX_ENOPERM_NODE]    = "Permission node not found",
    [XNIX_EENDPOINT]       = "Invalid endpoint",
    [XNIX_ETIMEOUT]        = "Operation timed out",
};

#define ERROR_MESSAGES_COUNT (sizeof(error_messages) / sizeof(error_messages[0]))

const char *strerror(int errnum) {
    if (errnum < 0 || (size_t)errnum >= ERROR_MESSAGES_COUNT) {
        return "Unknown error";
    }

    const char *msg = error_messages[errnum];
    return msg ? msg : "Unknown error";
}

void perror(const char *s) {
    if (s && *s) {
        printf("%s: %s\n", s, strerror(errno));
    } else {
        printf("%s\n", strerror(errno));
    }
}
