/**
 * @file xnix/errno.h
 * @brief 内核错误码定义(基于 ABI 层)
 *
 * 内核代码包含此头文件使用标准 errno.
 * 所有定义来自 ABI 层,确保内核和用户态使用相同的错误码值.
 */

#ifndef XNIX_ERRNO_H
#define XNIX_ERRNO_H

/*
 * 定义 _KERNEL 宏,告诉 ABI 层这是内核代码
 */
#define _KERNEL

/*
 * 包含 ABI 层的错误码定义(XNIX_E* 前缀)
 */
#include <xnix/abi/errno.h>

/*
 * 内核便捷别名(不带 XNIX_ 前缀)
 * 映射到 ABI 层的稳定定义
 */
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

#endif /* XNIX_ERRNO_H */
