/**
 * @file capability.h
 * @brief 能力系统完整定义
 *
 * 能力系统提供基于能力的访问控制,进程通过能力句柄访问内核对象.
 * 公共 API 见 <xnix/capability.h>
 */

#ifndef KERNEL_CAPABILITY_H
#define KERNEL_CAPABILITY_H

#include <xnix/capability.h>
#include <xnix/config.h>
#include <xnix/sync.h>
#include <xnix/types.h>

struct process; /* 前向声明 */

/**
 * 能力表项
 */
struct capability {
    cap_type_t   type;
    cap_rights_t rights;
    void        *object; /* 指向实际内核对象 */
};

/**
 * 进程能力表
 */
struct cap_table {
    struct capability caps[CFG_CAP_TABLE_SIZE];
    spinlock_t        lock;
};

/**
 * 创建能力表
 */
struct cap_table *cap_table_create(void);

/**
 * 销毁能力表(释放所有能力)z
 */
void cap_table_destroy(struct cap_table *table);

/**
 * 分配句柄,返回句柄编号
 *
 * @param proc   目标进程
 * @param type   对象类型
 * @param object 对象指针
 * @param rights 权限
 * @return 句柄,失败返回 CAP_HANDLE_INVALID
 */
cap_handle_t cap_alloc(struct process *proc, cap_type_t type, void *object, cap_rights_t rights);

/**
 * 释放句柄
 *
 * @param proc   目标进程
 * @param handle 句柄
 */
void cap_free(struct process *proc, cap_handle_t handle);

/**
 * 查找对象(会检查类型和权限)
 *
 * @param proc           目标进程
 * @param handle         句柄
 * @param expected_type  期望的对象类型
 * @param required_rights 需要的权限
 * @return 对象指针,失败返回 NULL
 */
void *cap_lookup(struct process *proc, cap_handle_t handle, cap_type_t expected_type,
                 cap_rights_t required_rights);

/**
 * 复制能力到其他进程(需要 CAP_GRANT 权限)
 *
 * @param src        源进程
 * @param src_handle 源句柄
 * @param dst        目标进程
 * @param new_rights 新权限(必须 <= 原权限)
 * @return 目标进程中的新句柄,失败返回 CAP_HANDLE_INVALID
 */
cap_handle_t cap_duplicate_to(struct process *src, cap_handle_t src_handle, struct process *dst,
                              cap_rights_t new_rights);

/**
 * 对象引用计数(由各对象类型实现)
 */
typedef void (*cap_ref_fn)(void *object);
typedef void (*cap_unref_fn)(void *object);

/**
 * 注册对象类型的引用计数函数
 */
void cap_register_type(cap_type_t type, cap_ref_fn ref, cap_unref_fn unref);

#endif
