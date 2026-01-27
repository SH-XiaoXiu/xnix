/**
 * @file abi/capability.h
 * @brief 能力系统 ABI 定义
 */

#ifndef XNIX_ABI_CAPABILITY_H
#define XNIX_ABI_CAPABILITY_H

#include <xnix/abi/stdint.h>
#include <xnix/abi/types.h>

/*
 * 能力权限位
 */
#define ABI_CAP_READ   (1 << 0) /* 可读取(如 receive 消息) */
#define ABI_CAP_WRITE  (1 << 1) /* 可写入(如 send 消息) */
#define ABI_CAP_GRANT  (1 << 2) /* 可委派(转移能力给其他进程) */
#define ABI_CAP_MANAGE (1 << 3) /* 可管理(销毁对象等) */

typedef uint32_t cap_rights_t;

/*
 * 能力对象类型
 */
typedef enum {
    ABI_CAP_TYPE_NONE         = 0,
    ABI_CAP_TYPE_ENDPOINT     = 1, /* IPC endpoint */
    ABI_CAP_TYPE_NOTIFICATION = 2, /* 异步通知 */
    ABI_CAP_TYPE_IOPORT       = 3, /* I/O 端口授权 */
    ABI_CAP_TYPE_VMAR         = 4, /* 虚拟内存区域 */
    ABI_CAP_TYPE_THREAD       = 5, /* 线程 */
    ABI_CAP_TYPE_PROCESS      = 6, /* 进程 */
} abi_cap_type_t;

/*
 * spawn 系统调用参数
 */
struct abi_spawn_cap {
    uint32_t src;      /* 源 capability handle */
    uint32_t rights;   /* 授予的权限 */
    uint32_t dst_hint; /* 期望的目标 handle(-1 表示任意) */
};

#define ABI_SPAWN_MAX_CAPS 8
#define ABI_SPAWN_NAME_LEN 16

struct abi_spawn_args {
    char                 name[ABI_SPAWN_NAME_LEN]; /* 进程名 */
    uint32_t             module_index;             /* 启动模块索引 */
    uint32_t             cap_count;                /* 传递的 capability 数量 */
    struct abi_spawn_cap caps[ABI_SPAWN_MAX_CAPS]; /* 传递的 capabilities */
};

#endif /* XNIX_ABI_CAPABILITY_H */
