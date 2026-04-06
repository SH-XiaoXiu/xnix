/**
 * @file ipc_endpoint.c
 * @brief libsys endpoint cache helpers
 */

#include <string.h>
#include <xnix/sys/ipc.h>
#include <xnix/syscall.h>

struct sys_ep_cache_entry {
    char     name[32];
    handle_t handle;
};

#define SYS_EP_CACHE_SIZE 8
static struct sys_ep_cache_entry g_ep_cache[SYS_EP_CACHE_SIZE];
static uint32_t                  g_ep_cache_next = 0;

handle_t sys_ipc_ep_find(const char *name) {
    if (!name) {
        return HANDLE_INVALID;
    }

    for (uint32_t i = 0; i < SYS_EP_CACHE_SIZE; i++) {
        if (g_ep_cache[i].name[0] != '\0' && strcmp(g_ep_cache[i].name, name) == 0) {
            return g_ep_cache[i].handle;
        }
    }

    handle_t handle = sys_handle_find(name);
    if (handle == HANDLE_INVALID) {
        return HANDLE_INVALID;
    }

    uint32_t idx = g_ep_cache_next % SYS_EP_CACHE_SIZE;
    g_ep_cache_next++;
    strncpy(g_ep_cache[idx].name, name, sizeof(g_ep_cache[idx].name) - 1);
    g_ep_cache[idx].name[sizeof(g_ep_cache[idx].name) - 1] = '\0';
    g_ep_cache[idx].handle = handle;
    return handle;
}

void sys_ipc_ep_clear_cache(const char *name) {
    if (!name) {
        memset(g_ep_cache, 0, sizeof(g_ep_cache));
        g_ep_cache_next = 0;
        return;
    }

    for (uint32_t i = 0; i < SYS_EP_CACHE_SIZE; i++) {
        if (strcmp(g_ep_cache[i].name, name) == 0) {
            g_ep_cache[i].name[0] = '\0';
            g_ep_cache[i].handle = HANDLE_INVALID;
            return;
        }
    }
}
