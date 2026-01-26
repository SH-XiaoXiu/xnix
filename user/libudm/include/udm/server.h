/**
 * @file server.h
 * @brief UDM Server 框架
 */

#ifndef UDM_SERVER_H
#define UDM_SERVER_H

#include <xnix/ipc.h>

/* 消息处理函数类型 */
typedef int (*udm_handler_t)(struct ipc_message *msg);

/* UDM Server 配置 */
struct udm_server {
    cap_handle_t  endpoint; /* 从内核继承的 endpoint */
    udm_handler_t handler;  /* 消息处理函数 */
    const char   *name;     /* 服务名称（用于调试） */
};

/**
 * 初始化 UDM server
 */
void udm_server_init(struct udm_server *srv);

/**
 * 运行 UDM server 主循环（永不返回）
 */
void udm_server_run(struct udm_server *srv);

#endif /* UDM_SERVER_H */
