/**
 * @file capability.h
 * @brief 能力系统 API
 *
 * 能力是对内核对象的访问权限,类似文件描述符, 不过cap是内核IPC层面的.
 * 完整定义见 <kernel/cap/capability.h>
 */

#ifndef XNIX_CAPABILITY_H
#define XNIX_CAPABILITY_H

#include <xnix/types.h>

/**
 * 能力句柄, 类似颁发了一个token
 */
typedef uint32_t cap_handle_t;
#define CAP_HANDLE_INVALID ((cap_handle_t) - 1)

/**
 * 能力权限位
 */
#define CAP_READ   (1 << 0) /* 可读取(如 receive 消息) */
#define CAP_WRITE  (1 << 1) /* 可写入(如 send 消息) */
#define CAP_GRANT  (1 << 2) /* 可委派(转移能力给其他进程) */
#define CAP_MANAGE (1 << 3) /* 可管理(销毁对象等) */

typedef uint32_t cap_rights_t;

/**
 * 对象类型
 */
typedef enum {
    CAP_TYPE_NONE = 0,
    CAP_TYPE_ENDPOINT,     /* IPC endpoint */
    CAP_TYPE_NOTIFICATION, /* 异步通知 */
    CAP_TYPE_IOPORT,       /* I/O 端口范围授权(配合 SYS_IOPORT_INB/OUTB) */
    CAP_TYPE_VMAR,         /* 虚拟内存区域(共享内存) */
    CAP_TYPE_THREAD,       /* 线程 */
    CAP_TYPE_PROCESS,      /* 进程 */
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
