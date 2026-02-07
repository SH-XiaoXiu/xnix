/**
 * @file ramfsd_service.h
 * @brief Init 内置的 ramfsd 服务
 *
 * ramfsd 作为 init 进程的一个服务线程运行,提供内存文件系统服务.
 */

#ifndef RAMFSD_SERVICE_H
#define RAMFSD_SERVICE_H

#include "ramfs.h"

#include <pthread.h>
#include <stdbool.h>
#include <xnix/abi/handle.h>

/**
 * ramfsd 服务上下文
 */
struct ramfsd_service {
    struct ramfs_ctx ramfs;    /* ramfs 上下文 */
    handle_t         endpoint; /* 服务 endpoint */
    pthread_t        thread;   /* 服务线程 */
    bool             running;  /* 运行标志 */
};

/**
 * 启动 ramfsd 服务线程
 *
 * @param service ramfsd 服务上下文
 * @return 0 成功, <0 失败
 */
int ramfsd_service_start(struct ramfsd_service *service);

/**
 * 停止 ramfsd 服务线程
 *
 * @param service ramfsd 服务上下文
 */
void ramfsd_service_stop(struct ramfsd_service *service);

/**
 * 获取 ramfs 上下文(用于 initramfs 提取)
 *
 * @param service ramfsd 服务上下文
 * @return ramfs 上下文指针
 */
struct ramfs_ctx *ramfsd_service_get_ramfs(struct ramfsd_service *service);

#endif /* RAMFSD_SERVICE_H */
