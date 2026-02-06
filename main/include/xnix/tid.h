/**
 * @file tid.h
 * @brief TID 资源管理接口
 *
 * 此文件位于共享层,供 kernel 和 lib 等组件使用.
 */

#ifndef XNIX_TID_H
#define XNIX_TID_H

#include <xnix/types.h>

/**
 * 初始化 TID 管理模块
 */
void tid_init(void);

/**
 * 分配一个新的 TID
 *
 * @return 分配的 TID,失败返回 TID_INVALID
 */
tid_t tid_alloc(void);

/**
 * 释放一个 TID
 *
 * @param tid 要释放的 TID
 */
void tid_free(tid_t tid);

#endif
