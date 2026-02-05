/**
 * @file endpoint.c
 * @brief Endpoint 缓存实现
 */

#include <ipc/client.h>
#include <string.h>
#include <xnix/syscall.h>

/* Endpoint 缓存项 */
struct ep_cache_entry {
    char     name[32];
    handle_t handle;
};

/* 固定大小缓存(8 项) */
#define EP_CACHE_SIZE 8
static struct ep_cache_entry g_ep_cache[EP_CACHE_SIZE];
static uint32_t              g_ep_cache_next = 0; /* LRU 替换索引 */

handle_t ipc_ep_find(const char *name) {
    if (!name) {
        return HANDLE_INVALID;
    }

    /* 先查找缓存 */
    for (uint32_t i = 0; i < EP_CACHE_SIZE; i++) {
        if (g_ep_cache[i].name[0] != '\0' && strcmp(g_ep_cache[i].name, name) == 0) {
            return g_ep_cache[i].handle;
        }
    }

    /* 未命中,查找并缓存 */
    handle_t handle = sys_handle_find(name);
    if (handle == HANDLE_INVALID) {
        return HANDLE_INVALID;
    }

    /* 添加到缓存(LRU 策略) */
    uint32_t idx = g_ep_cache_next % EP_CACHE_SIZE;
    g_ep_cache_next++;

    strncpy(g_ep_cache[idx].name, name, sizeof(g_ep_cache[idx].name) - 1);
    g_ep_cache[idx].name[sizeof(g_ep_cache[idx].name) - 1] = '\0';
    g_ep_cache[idx].handle                                 = handle;

    return handle;
}

void ipc_ep_clear_cache(const char *name) {
    if (!name) {
        /* 清除所有 */
        memset(g_ep_cache, 0, sizeof(g_ep_cache));
        g_ep_cache_next = 0;
        return;
    }

    /* 清除特定条目 */
    for (uint32_t i = 0; i < EP_CACHE_SIZE; i++) {
        if (strcmp(g_ep_cache[i].name, name) == 0) {
            g_ep_cache[i].name[0] = '\0';
            g_ep_cache[i].handle  = HANDLE_INVALID;
            return;
        }
    }
}
