/**
 * @file abi/protocol.h
 * @brief UDM 协议层错误码定义
 *
 * 协议错误码与系统调用错误码分离:
 * - 系统调用错误码(errno):IPC 传输层失败(如 endpoint 无效,权限不足,超时)
 * - 协议错误码:IPC 消息体内业务逻辑失败(如 VFS 文件不存在,Serial 设备忙)
 *
 * 协议错误码在 IPC 消息的响应字段中传递,独立于系统调用返回值.
 */

#ifndef XNIX_ABI_PROTOCOL_H
#define XNIX_ABI_PROTOCOL_H

#include <xnix/abi/errno.h>

/*
 * UDM 协议基础错误码
 * 所有 UDM 协议共享这些基础错误码
 */
#define UDM_OK           0    /* Success */
#define UDM_ERR_UNKNOWN  (-1) /* Unknown error */
#define UDM_ERR_INVALID  (-2) /* Invalid argument */
#define UDM_ERR_NOTFOUND (-3) /* Not found */
#define UDM_ERR_NOTSUP   (-4) /* Operation not supported */
#define UDM_ERR_BUSY     (-5) /* Device or resource busy */
#define UDM_ERR_IO       (-6) /* I/O error */
#define UDM_ERR_TIMEOUT  (-7) /* Operation timed out */
#define UDM_ERR_OVERFLOW (-8) /* Buffer overflow */
#define UDM_ERR_PERM     (-9) /* Permission denied */

/*
 * 特定协议可以扩展自己的错误码(值 < -100)
 * 例如:
 * #define VFS_ERR_READONLY  (-101)
 * #define FB_ERR_BADFORMAT  (-102)
 */

/**
 * 将系统调用 errno 转换为 UDM 协议错误码
 *
 * @param errnum 系统调用错误码(正值)
 * @return UDM 协议错误码(负值或 0)
 */
static inline int errno_to_udm(int errnum) {
    switch (errnum) {
    case XNIX_EOK:
        return UDM_OK;
    case XNIX_EINVAL:
        return UDM_ERR_INVALID;
    case XNIX_ENOENT:
        return UDM_ERR_NOTFOUND;
    case XNIX_ENOSYS:
        return UDM_ERR_NOTSUP;
    case XNIX_EBUSY:
        return UDM_ERR_BUSY;
    case XNIX_EIO:
        return UDM_ERR_IO;
    case XNIX_ETIMEDOUT:
        return UDM_ERR_TIMEOUT;
    case XNIX_EOVERFLOW:
        return UDM_ERR_OVERFLOW;
    case XNIX_EPERM:
    case XNIX_EACCES:
        return UDM_ERR_PERM;
    default:
        return UDM_ERR_UNKNOWN;
    }
}

/**
 * 将 UDM 协议错误码转换为系统调用 errno
 *
 * @param udm_err UDM 协议错误码(负值或 0)
 * @return 系统调用错误码(正值或 0)
 */
static inline int udm_to_errno(int udm_err) {
    switch (udm_err) {
    case UDM_OK:
        return XNIX_EOK;
    case UDM_ERR_INVALID:
        return XNIX_EINVAL;
    case UDM_ERR_NOTFOUND:
        return XNIX_ENOENT;
    case UDM_ERR_NOTSUP:
        return XNIX_ENOSYS;
    case UDM_ERR_BUSY:
        return XNIX_EBUSY;
    case UDM_ERR_IO:
        return XNIX_EIO;
    case UDM_ERR_TIMEOUT:
        return XNIX_ETIMEDOUT;
    case UDM_ERR_OVERFLOW:
        return XNIX_EOVERFLOW;
    case UDM_ERR_PERM:
        return XNIX_EPERM;
    case UDM_ERR_UNKNOWN:
    default:
        return XNIX_EIO; /* 未知错误映射为 I/O 错误 */
    }
}

#endif /* XNIX_ABI_PROTOCOL_H */
