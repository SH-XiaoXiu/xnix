/**
 * @file xnix/protocol/svc.h
 * @brief 服务管理 IPC 协议定义
 *
 * init 与服务之间的就绪通知协议.
 */

#ifndef XNIX_PROTOCOL_SVC_H
#define XNIX_PROTOCOL_SVC_H

#include <stdint.h>

#define SVC_MSG_READY 0xF001

struct svc_ready_msg {
    uint32_t magic;    /* SVC_MSG_READY */
    uint32_t pid;      /* 进程 ID */
    char     name[16]; /* 服务名 */
};

#endif /* XNIX_PROTOCOL_SVC_H */
