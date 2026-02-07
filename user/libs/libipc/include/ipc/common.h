/**
 * @file common.h
 * @brief IPC 辅助库公共定义
 */

#ifndef IPC_COMMON_H
#define IPC_COMMON_H

#include <xnix/abi/handle.h>
#include <xnix/abi/ipc.h>
#include <xnix/abi/types.h>

/*
 * 错误处理
 *
 * libipc 函数失败时返回 -1 并设置 errno(定义在 <errno.h>):
 * - EINVAL: 无效参数
 * - ENOENT: Endpoint 未找到
 * - ETIMEDOUT: 超时
 * - E2BIG: 参数过多
 * - EIO: 发送/接收失败
 */

/* 常量 */
#define IPC_MAX_ARGS 7 /* 最多 7 个参数(8 个寄存器 - 1 个 opcode) */

#endif /* IPC_COMMON_H */
