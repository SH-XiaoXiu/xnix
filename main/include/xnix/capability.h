/**
 * @file capability.h
 * @brief 能力系统 API
 *
 * 能力是对内核对象的访问权限,类似文件描述符.
 * 完整定义见 <kernel/cap/capability.h>
 */

#ifndef XNIX_CAPABILITY_H
#define XNIX_CAPABILITY_H

#include <xnix/abi/capability.h>
#include <xnix/types.h>

/* 权限位 */
#define CAP_READ   ABI_CAP_READ
#define CAP_WRITE  ABI_CAP_WRITE
#define CAP_GRANT  ABI_CAP_GRANT
#define CAP_MANAGE ABI_CAP_MANAGE

/* 对象类型 */
typedef enum {
    CAP_TYPE_NONE         = ABI_CAP_TYPE_NONE,
    CAP_TYPE_ENDPOINT     = ABI_CAP_TYPE_ENDPOINT,
    CAP_TYPE_NOTIFICATION = ABI_CAP_TYPE_NOTIFICATION,
    CAP_TYPE_IOPORT       = ABI_CAP_TYPE_IOPORT,
    CAP_TYPE_VMAR         = ABI_CAP_TYPE_VMAR,
    CAP_TYPE_THREAD       = ABI_CAP_TYPE_THREAD,
    CAP_TYPE_PROCESS      = ABI_CAP_TYPE_PROCESS,
} cap_type_t;

/**
 * 关闭能力句柄
 *
 * @param handle 要关闭的句柄
 * @return 0 成功,负数失败
 */
int cap_close(cap_handle_t handle);

/**
 * 复制能力句柄(增加引用计数)
 *
 * @param handle     源句柄
 * @param new_rights 新句柄的权限(必须 <= 原权限)
 * @return 新句柄,失败返回 CAP_HANDLE_INVALID
 */
cap_handle_t cap_duplicate(cap_handle_t handle, cap_rights_t new_rights);

#endif
