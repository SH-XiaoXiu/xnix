/**
 * @file handle.h
 * @brief 句柄 (Handle) 系统接口
 *
 * Handle 系统提供了内核对象的统一访问机制.
 * 用户态程序通过 handle 引用内核对象(如 Endpoint, Process 等).
 * Handle 表是每个进程私有的,实现了权限控制和引用计数.
 */

#ifndef XNIX_HANDLE_H
#define XNIX_HANDLE_H

#include <xnix/abi/handle.h>
#include <xnix/perm.h>
#include <xnix/sync.h>
#include <xnix/types.h>

/**
 * @brief Handle 表项
 *
 * 存储 handle 与内核对象的映射关系,以及访问权限.
 */
struct handle_entry {
    handle_type_t type;                   /* 对象类型 */
    void         *object;                 /* 内核对象指针 */
    char          name[HANDLE_NAME_MAX]; /* 可选名称(用于按名查找) */

    /* 缓存的权限 ID(用于加速 syscall 检查) */
    perm_id_t perm_send; /* 用于 HANDLE_ENDPOINT: 发送权限 */
    perm_id_t perm_recv; /* 用于 HANDLE_ENDPOINT: 接收权限 */
};

/**
 * @brief Handle 表
 *
 * 进程私有的 handle 映射表.支持动态扩容.
 */
struct handle_table {
    struct handle_entry *entries;  /* handle 数组 */
    uint32_t             capacity; /* 当前容量 */
    spinlock_t           lock;     /* 保护表的自旋锁 */
};

/* 表管理函数 */

/**
 * @brief 创建新的 Handle 表
 *
 * @return 新创建的 Handle 表指针,失败返回 NULL
 */
struct handle_table *handle_table_create(void);

/**
 * @brief 销毁 Handle 表
 *
 * 释放表占用的内存.注意:不会自动释放引用的内核对象,
 * 调用者需确保对象引用计数已正确处理.
 *
 * @param table 要销毁的 Handle 表
 */
void handle_table_destroy(struct handle_table *table);

/**
 * @brief 获取 Handle 表项
 *
 * @param table Handle 表
 * @param h     Handle 值
 * @return 指向表项的指针,如果 handle 无效则返回 NULL
 */
struct handle_entry *handle_get_entry(struct handle_table *table, handle_t h);

/* Handle 分配函数 */

/**
 * @brief 分配一个新的 Handle
 *
 * 在指定进程的 handle 表中分配一个空闲 slot,并关联对象.
 *
 * @param proc   目标进程
 * @param type   对象类型
 * @param object 对象指针
 * @param name   Handle 名称(可选,可为 NULL)
 * @return 分配的 handle 值,失败返回 HANDLE_INVALID
 */
handle_t handle_alloc(struct process *proc, handle_type_t type, void *object, const char *name);

/**
 * @brief 在指定位置分配 Handle
 *
 * 尝试在指定的 slot 分配 handle.如果该位置已被占用,则分配失败.
 *
 * @param proc   目标进程
 * @param type   对象类型
 * @param object 对象指针
 * @param name   Handle 名称(可选,可为 NULL)
 * @param hint   期望的 handle 值
 * @return 分配的 handle 值(等于 hint),失败返回 HANDLE_INVALID
 */
handle_t handle_alloc_at(struct process *proc, handle_type_t type, void *object, const char *name,
                         handle_t hint);

/**
 * @brief 释放 Handle
 *
 * 清除 handle 表项.
 *
 * @param proc 目标进程
 * @param h    要释放的 handle
 */
void handle_free(struct process *proc, handle_t h);

/* Handle 解析函数 */

/**
 * @brief 解析 Handle 为内核对象
 *
 * 验证 handle 的有效性,类型和权限,并返回对应的内核对象.
 *
 * @param proc          调用进程
 * @param h             Handle 值
 * @param expected_type 期望的对象类型 (HANDLE_NONE 表示不检查类型)
 * @param required_perm 需要的权限 ID (PERM_ID_INVALID 表示不检查权限)
 * @return 内核对象指针,验证失败返回 NULL
 */
void *handle_resolve(struct process *proc, handle_t h, handle_type_t expected_type,
                     perm_id_t required_perm);

/**
 * @brief 按名称查找 Handle
 *
 * 在进程的 handle 表中查找具有指定名称的 handle.
 *
 * @param proc 目标进程
 * @param name 要查找的名称
 * @return 找到的 handle 值,未找到返回 HANDLE_INVALID
 */
handle_t handle_find(struct process *proc, const char *name);

/* Handle 传递函数 */

/**
 * @brief 传递 Handle 给另一个进程
 *
 * 将源进程中的 handle 复制到目标进程.
 *
 * @param src      源进程
 * @param src_h    源 handle
 * @param dst      目标进程
 * @param name     在目标进程中的名称(可选,NULL 则沿用原名)
 * @param dst_hint 在目标进程中的期望 slot (-1 表示自动分配)
 * @return 目标进程中的 handle 值,失败返回 HANDLE_INVALID
 */
handle_t handle_transfer(struct process *src, handle_t src_h, struct process *dst, const char *name,
                         handle_t dst_hint);

#endif /* XNIX_HANDLE_H */
