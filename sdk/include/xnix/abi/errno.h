/**
 * @file abi/errno.h
 * @brief POSIX 标准错误码 ABI 定义
 *
 * 约定:
 * - 系统调用返回负值错误码(如 -EINVAL)
 * - 用户态包装函数转换为 -1 并设置 errno
 * - 0 表示成功
 *
 * 所有错误码使用 XNIX_E* 前缀作为 ABI 稳定名称.
 * 用户态可使用不带前缀的便捷别名(E*).
 */

#ifndef XNIX_ABI_ERRNO_H
#define XNIX_ABI_ERRNO_H

/*
 * POSIX 标准错误码(1-40)
 */
#define XNIX_EOK          0  /* Success */
#define XNIX_EPERM        1  /* Operation not permitted */
#define XNIX_ENOENT       2  /* No such file or directory */
#define XNIX_ESRCH        3  /* No such process */
#define XNIX_EINTR        4  /* Interrupted system call */
#define XNIX_EIO          5  /* I/O error */
#define XNIX_ENXIO        6  /* No such device or address */
#define XNIX_E2BIG        7  /* Argument list too long */
#define XNIX_ENOEXEC      8  /* Exec format error */
#define XNIX_EBADF        9  /* Bad file descriptor */
#define XNIX_ECHILD       10 /* No child processes */
#define XNIX_EAGAIN       11 /* Try again (would block) */
#define XNIX_ENOMEM       12 /* Out of memory */
#define XNIX_EACCES       13 /* Permission denied */
#define XNIX_EFAULT       14 /* Bad address */
#define XNIX_ENOTBLK      15 /* Block device required */
#define XNIX_EBUSY        16 /* Device or resource busy */
#define XNIX_EEXIST       17 /* File exists */
#define XNIX_EXDEV        18 /* Cross-device link */
#define XNIX_ENODEV       19 /* No such device */
#define XNIX_ENOTDIR      20 /* Not a directory */
#define XNIX_EISDIR       21 /* Is a directory */
#define XNIX_EINVAL       22 /* Invalid argument */
#define XNIX_ENFILE       23 /* File table overflow */
#define XNIX_EMFILE       24 /* Too many open files */
#define XNIX_ENOTTY       25 /* Not a typewriter */
#define XNIX_ETXTBSY      26 /* Text file busy */
#define XNIX_EFBIG        27 /* File too large */
#define XNIX_ENOSPC       28 /* No space left on device */
#define XNIX_ESPIPE       29 /* Illegal seek */
#define XNIX_EROFS        30 /* Read-only file system */
#define XNIX_EMLINK       31 /* Too many links */
#define XNIX_EPIPE        32 /* Broken pipe */
#define XNIX_EDOM         33 /* Math argument out of domain */
#define XNIX_ERANGE       34 /* Math result not representable */
#define XNIX_EDEADLK      35 /* Resource deadlock would occur */
#define XNIX_ENAMETOOLONG 36 /* File name too long */
#define XNIX_ENOLCK       37 /* No record locks available */
#define XNIX_ENOSYS       38 /* Function not implemented */
#define XNIX_ENOTEMPTY    39 /* Directory not empty */
#define XNIX_ELOOP        40 /* Too many symbolic links */

/*
 * 扩展 POSIX 错误码(41-59)
 */
#define XNIX_ENOMSG    41           /* No message of desired type */
#define XNIX_EIDRM     42           /* Identifier removed */
#define XNIX_EDEADLOCK XNIX_EDEADLK /* Alias */
#define XNIX_ENODATA   43           /* No data available */
#define XNIX_ENOSTR    44           /* Device not a stream */
#define XNIX_EPROTO    45           /* Protocol error */
#define XNIX_EBADMSG   46           /* Not a data message */
#define XNIX_EOVERFLOW 47           /* Value too large for type */
#define XNIX_EILSEQ    48           /* Illegal byte sequence */

/*
 * 网络相关错误码(50-59)
 */
#define XNIX_ENOTSOCK        50 /* Socket operation on non-socket */
#define XNIX_EDESTADDRREQ    51 /* Destination address required */
#define XNIX_EMSGSIZE        52 /* Message too long */
#define XNIX_EPROTOTYPE      53 /* Protocol wrong type for socket */
#define XNIX_ENOPROTOOPT     54 /* Protocol not available */
#define XNIX_EPROTONOSUPPORT 55 /* Protocol not supported */
#define XNIX_EAFNOSUPPORT    56 /* Address family not supported */
#define XNIX_EADDRINUSE      57 /* Address already in use */
#define XNIX_EADDRNOTAVAIL   58 /* Cannot assign requested address */
#define XNIX_ENETDOWN        59 /* Network is down */

/*
 * 微内核特定错误码(60-69)
 */
#define XNIX_ENOTCONN     60 /* Transport endpoint not connected */
#define XNIX_ESHUTDOWN    61 /* Endpoint has been shut down */
#define XNIX_ENOHANDLE    62 /* Invalid handle */
#define XNIX_ENOPERM_NODE 63 /* Permission node not found */
#define XNIX_EENDPOINT    64 /* Invalid endpoint */
#define XNIX_ETIMEOUT     65 /* Operation timed out */

/*
 * 通用别名
 */
#define XNIX_EWOULDBLOCK XNIX_EAGAIN   /* Operation would block */
#define XNIX_ETIMEDOUT   XNIX_ETIMEOUT /* Connection timed out (alias) */

/*
 * 用户态便捷别名(不带 XNIX_ 前缀)
 * 仅在非内核代码中定义,避免命名空间污染
 */
#ifndef _KERNEL

#define EOK             XNIX_EOK
#define EPERM           XNIX_EPERM
#define ENOENT          XNIX_ENOENT
#define ESRCH           XNIX_ESRCH
#define EINTR           XNIX_EINTR
#define EIO             XNIX_EIO
#define ENXIO           XNIX_ENXIO
#define E2BIG           XNIX_E2BIG
#define ENOEXEC         XNIX_ENOEXEC
#define EBADF           XNIX_EBADF
#define ECHILD          XNIX_ECHILD
#define EAGAIN          XNIX_EAGAIN
#define ENOMEM          XNIX_ENOMEM
#define EACCES          XNIX_EACCES
#define EFAULT          XNIX_EFAULT
#define ENOTBLK         XNIX_ENOTBLK
#define EBUSY           XNIX_EBUSY
#define EEXIST          XNIX_EEXIST
#define EXDEV           XNIX_EXDEV
#define ENODEV          XNIX_ENODEV
#define ENOTDIR         XNIX_ENOTDIR
#define EISDIR          XNIX_EISDIR
#define EINVAL          XNIX_EINVAL
#define ENFILE          XNIX_ENFILE
#define EMFILE          XNIX_EMFILE
#define ENOTTY          XNIX_ENOTTY
#define ETXTBSY         XNIX_ETXTBSY
#define EFBIG           XNIX_EFBIG
#define ENOSPC          XNIX_ENOSPC
#define ESPIPE          XNIX_ESPIPE
#define EROFS           XNIX_EROFS
#define EMLINK          XNIX_EMLINK
#define EPIPE           XNIX_EPIPE
#define EDOM            XNIX_EDOM
#define ERANGE          XNIX_ERANGE
#define EDEADLK         XNIX_EDEADLK
#define ENAMETOOLONG    XNIX_ENAMETOOLONG
#define ENOLCK          XNIX_ENOLCK
#define ENOSYS          XNIX_ENOSYS
#define ENOTEMPTY       XNIX_ENOTEMPTY
#define ELOOP           XNIX_ELOOP
#define ENOMSG          XNIX_ENOMSG
#define EIDRM           XNIX_EIDRM
#define EDEADLOCK       XNIX_EDEADLOCK
#define ENODATA         XNIX_ENODATA
#define ENOSTR          XNIX_ENOSTR
#define EPROTO          XNIX_EPROTO
#define EBADMSG         XNIX_EBADMSG
#define EOVERFLOW       XNIX_EOVERFLOW
#define EILSEQ          XNIX_EILSEQ
#define ENOTSOCK        XNIX_ENOTSOCK
#define EDESTADDRREQ    XNIX_EDESTADDRREQ
#define EMSGSIZE        XNIX_EMSGSIZE
#define EPROTOTYPE      XNIX_EPROTOTYPE
#define ENOPROTOOPT     XNIX_ENOPROTOOPT
#define EPROTONOSUPPORT XNIX_EPROTONOSUPPORT
#define EAFNOSUPPORT    XNIX_EAFNOSUPPORT
#define EADDRINUSE      XNIX_EADDRINUSE
#define EADDRNOTAVAIL   XNIX_EADDRNOTAVAIL
#define ENETDOWN        XNIX_ENETDOWN
#define ENOTCONN        XNIX_ENOTCONN
#define ESHUTDOWN       XNIX_ESHUTDOWN
#define ENOHANDLE       XNIX_ENOHANDLE
#define ENOPERM_NODE    XNIX_ENOPERM_NODE
#define EENDPOINT       XNIX_EENDPOINT
#define ETIMEOUT        XNIX_ETIMEOUT
#define EWOULDBLOCK     XNIX_EWOULDBLOCK
#define ETIMEDOUT       XNIX_ETIMEDOUT

#endif /* !_KERNEL */

#endif /* XNIX_ABI_ERRNO_H */
