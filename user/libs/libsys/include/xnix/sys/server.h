/**
 * @file server.h
 * @brief 通用用户态服务循环辅助
 */

#ifndef XNIX_SYS_SERVER_H
#define XNIX_SYS_SERVER_H

#include <xnix/ipc.h>

typedef int (*sys_handler_t)(struct ipc_message *msg);

struct sys_server {
    handle_t      endpoint;
    sys_handler_t handler;
    const char   *name;
};

void sys_server_init(struct sys_server *srv);
void sys_server_run(struct sys_server *srv);

#endif
