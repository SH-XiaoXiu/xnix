/**
 * @file ramfsd_service.c
 * @brief Init 内置的 ramfsd 服务实现
 */

#include "ramfsd_service.h"

#include <d/server.h>
#include <pthread.h>
#include <stdio.h>
#include <vfs/vfs.h>
#include <xnix/syscall.h>

/* 全局 service 指针(用于 handler 访问) */
static struct ramfsd_service *g_service = NULL;

/* VFS 请求处理回调 */
static int vfs_handler(struct ipc_message *msg) {
    if (!g_service) {
        return -1;
    }
    return vfs_dispatch(ramfs_get_ops(), &g_service->ramfs, msg);
}

/* ramfsd 服务线程入口 */
static void *ramfsd_thread(void *arg) {
    struct ramfsd_service *service = arg;

    printf("[ramfsd] service thread started\n");

    /* 使用 udm_server 框架处理请求 */
    struct udm_server srv = {
        .endpoint = service->endpoint,
        .handler  = vfs_handler,
        .name     = "ramfsd",
    };

    udm_server_init(&srv);
    udm_server_run(&srv);

    printf("[ramfsd] service thread exiting\n");
    return NULL;
}

int ramfsd_service_start(struct ramfsd_service *service) {
    if (!service) {
        return -1;
    }

    /* 设置全局指针 */
    g_service = service;

    /* 初始化 ramfs */
    ramfs_init(&service->ramfs);
    printf("[ramfsd] ramfs initialized\n");

    /* 创建 endpoint */
    service->endpoint = sys_endpoint_create("ramfs_ep");
    if (service->endpoint == HANDLE_INVALID) {
        printf("[ramfsd] FATAL: failed to create endpoint\n");
        return -1;
    }

    printf("[ramfsd] created endpoint: %u\n", service->endpoint);

    /* 创建服务线程 */
    service->running = true;
    if (pthread_create(&service->thread, NULL, ramfsd_thread, service) != 0) {
        printf("[ramfsd] FATAL: failed to create thread\n");
        sys_handle_close(service->endpoint);
        return -1;
    }

    printf("[ramfsd] service thread created\n");
    return 0;
}

void ramfsd_service_stop(struct ramfsd_service *service) {
    if (!service || !service->running) {
        return;
    }

    service->running = false;

    /* TODO: 发送停止信号给线程 */
    /* pthread_join(service->thread, NULL); */

    sys_handle_close(service->endpoint);
}

struct ramfs_ctx *ramfsd_service_get_ramfs(struct ramfsd_service *service) {
    if (!service) {
        return NULL;
    }
    return &service->ramfs;
}
