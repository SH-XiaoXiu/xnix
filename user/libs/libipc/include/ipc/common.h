/**
 * @file common.h
 * @brief IPC 辅助库公共定义
 */

#ifndef IPC_COMMON_H
#define IPC_COMMON_H

#include <xnix/abi/handle.h>
#include <xnix/abi/ipc.h>
#include <xnix/abi/types.h>

/* 错误码 */
#define IPC_OK           0
#define IPC_ERR_INVALID  (-1) /* 无效参数 */
#define IPC_ERR_NOTFOUND (-2) /* Endpoint 未找到 */
#define IPC_ERR_TIMEOUT  (-3) /* 超时 */
#define IPC_ERR_OVERFLOW (-4) /* 参数过多 */
#define IPC_ERR_SEND     (-5) /* 发送失败 */
#define IPC_ERR_RECV     (-6) /* 接收失败 */

/* 常量 */
#define IPC_MAX_ARGS 7 /* 最多 7 个参数(8 个寄存器 - 1 个 opcode) */

#endif /* IPC_COMMON_H */
