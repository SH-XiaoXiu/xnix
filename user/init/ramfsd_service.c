/**
 * @file ramfsd_service.c
 * @brief Init 内置的 ramfsd 服务实现
 */

#include "ramfsd_service.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <vfs/vfs.h>
#include <xnix/abi/ipc.h>
#include <xnix/syscall.h>

/* 全局 service 指针(用于 handler 访问) */
static struct ramfsd_service *g_service = NULL;

/* 查找 file_ep 对应的 slot */
static int find_slot_for_ep(handle_t ep) {
    for (int i = 0; i < RAMFS_MAX_HANDLES; i++) {
        if (ramfs_get_file_ep(&g_service->ramfs, i) == ep) return i;
    }
    return -1;
}

/* ramfsd 自定义事件循环: 同时监听 main_ep 和所有 file_ep */
#define RAMFSD_RECV_BUF_SIZE 4096
static char g_ramfsd_recv_buf[RAMFSD_RECV_BUF_SIZE];

static void ramfsd_main_loop(handle_t main_ep) {
    struct ipc_message msg;

    while (1) {
        /* 构建 wait set: main_ep + 所有活跃 file_ep */
        struct abi_ipc_wait_set set = {0};
        set.handles[0] = main_ep;
        set.count = 1;
        for (int i = 0; i < RAMFS_MAX_HANDLES; i++) {
            handle_t ep = ramfs_get_file_ep(&g_service->ramfs, i);
            if (ep != HANDLE_INVALID && set.count < ABI_IPC_WAIT_MAX) {
                set.handles[set.count++] = ep;
            }
        }

        handle_t ready = sys_ipc_wait_any(&set, 0);
        if (ready == HANDLE_INVALID) continue;

        memset(&msg, 0, sizeof(msg));
        msg.buffer.data = (uint64_t)(uintptr_t)g_ramfsd_recv_buf;
        msg.buffer.size = RAMFSD_RECV_BUF_SIZE;

        if (sys_ipc_receive(ready, &msg, 0) < 0) continue;

        if (ready == main_ep) {
            vfs_dispatch(ramfs_get_ops(), &g_service->ramfs, &msg);
        } else {
            int slot = find_slot_for_ep(ready);
            if (slot >= 0) {
                ramfs_file_ep_dispatch(&g_service->ramfs, slot, &msg);
            }
        }
    }
}

/* ramfsd 服务线程入口 */
static void *ramfsd_thread(void *arg) {
    struct ramfsd_service *service = arg;

    printf("[ramfsd] service thread started\n");

    ramfsd_main_loop(service->endpoint);

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
