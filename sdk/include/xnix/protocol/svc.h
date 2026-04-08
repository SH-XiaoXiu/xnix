/**
 * @file xnix/protocol/svc.h
 * @brief 服务管理 IPC 协议定义
 *
 * init 与服务之间的就绪通知协议及管理命令.
 */

#ifndef XNIX_PROTOCOL_SVC_H
#define XNIX_PROTOCOL_SVC_H

#include <stdint.h>

#define SVC_MSG_READY 0xF001

/* 管理命令 opcode */
#define SVC_MSG_STOP    0xF010
#define SVC_MSG_START   0xF011
#define SVC_MSG_RESTART 0xF012
#define SVC_MSG_STATUS  0xF013
#define SVC_MSG_LIST    0xF014

/* 管理命令回复状态码 */
#define SVC_REPLY_OK        0
#define SVC_REPLY_NOT_FOUND 1
#define SVC_REPLY_ALREADY   2
#define SVC_REPLY_ERROR     3

struct svc_ready_msg {
    uint32_t magic;    /* SVC_MSG_READY */
    uint32_t pid;      /* 进程 ID */
    char     name[16]; /* 服务名 */
};

/*
 * 管理命令消息格式 (复用 ipc_message.regs):
 *   data[0] = opcode (SVC_MSG_STOP / START / RESTART / STATUS / LIST)
 *   data[1..4] = 服务名 (最多 16 字节, NUL 填充)
 *
 * 回复格式:
 *   data[0] = 状态码 (SVC_REPLY_*)
 *   data[1] = state  (svc_state_t)
 *   data[2] = pid
 *
 * LIST 命令: 服务列表通过 buffer 返回
 */

#endif /* XNIX_PROTOCOL_SVC_H */
