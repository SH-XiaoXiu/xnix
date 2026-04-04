/**
 * @file perm_api.h
 * @brief 用户态能力与 handle 自省 API
 */

#ifndef XNIX_PERM_API_H
#define XNIX_PERM_API_H

#include <stdint.h>
#include <xnix/abi/cap.h>
#include <xnix/abi/handle.h>

typedef int pid_t;

/**
 * 检查当前进程是否拥有指定能力
 *
 * @param cap 能力位 (CAP_*)
 * @return 1 有能力, 0 无能力
 */
int cap_has(uint32_t cap);

/**
 * 获取当前进程的能力位图
 *
 * @return cap_mask
 */
uint32_t cap_query(void);

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
