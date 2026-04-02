/**
 * @file perm_api.h
 * @brief 用户态权限操作高层 API
 *
 * 封装 syscall 层的权限和 handle 自省接口,
 * 提供回调式遍历和简单的权限检查.
 */

#ifndef XNIX_PERM_API_H
#define XNIX_PERM_API_H

#include <stdint.h>
#include <xnix/abi/handle.h>

typedef int pid_t;

/**
 * 检查当前进程是否拥有指定权限
 *
 * @param node 权限节点名(如 "xnix.ipc.send")
 * @return 1 有权限, 0 无权限, -1 错误
 */
int perm_has(const char *node);

/**
 * 委托权限给目标进程
 *
 * @param pid  目标进程 ID
 * @param node 权限节点名
 * @return 0 成功, -1 失败(设置 errno)
 */
int perm_grant_to(pid_t pid, const char *node);

/**
 * 撤销目标进程的权限
 *
 * @param pid  目标进程 ID
 * @param node 权限节点名
 * @return 0 成功, -1 失败(设置 errno)
 */
int perm_revoke_from(pid_t pid, const char *node);

/**
 * 遍历当前进程拥有的权限
 *
 * @param cb  回调函数: node=权限名, granted=是否授予, ctx=用户上下文
 * @param ctx 传给回调的上下文
 * @return 遍历的条目数, -1 失败
 */
int perm_list(void (*cb)(const char *node, int granted, void *ctx), void *ctx);

/**
 * 遍历当前进程的 handle 列表
 *
 * @param cb  回调函数: h=handle 值, type=类型, rights=权限位, name=名称, ctx=上下文
 * @param ctx 传给回调的上下文
 * @return 遍历的条目数, -1 失败
 */
int handle_list_fds(void (*cb)(uint32_t h, int type, uint32_t rights,
                               const char *name, void *ctx), void *ctx);

#endif /* XNIX_PERM_API_H */
