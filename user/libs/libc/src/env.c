/**
 * @file env.c
 * @brief 环境变量和 handle 查找实现
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/syscall.h>

/**
 * Handle 映射缓存
 */
struct handle_cache {
    char     name[32];
    uint32_t handle;
    bool     valid;
};

/* Handle 缓存 */
static struct handle_cache g_handle_cache[16];
static int                 g_cache_size = 0;

/* 进程显示名(用于 env_require 错误消息) */
static char g_env_name[16] = "?";

/**
 * 初始化环境 handle 映射(预留接口,暂未使用)
 */
void __env_init_handles(const char **handle_names, uint32_t *handle_values, int count) {
    (void)handle_names;
    (void)handle_values;
    (void)count;
    /* 暂不需要预初始化,使用按需查找+缓存策略 */
}

/**
 * 根据名称查找 handle
 *
 * 实现策略:首次查找时调用 sys_handle_find,结果缓存后续使用
 */
uint32_t env_get_handle(const char *name) {
    /* 先查缓存 */
    for (int i = 0; i < g_cache_size; i++) {
        if (g_handle_cache[i].valid && strcmp(g_handle_cache[i].name, name) == 0) {
            return g_handle_cache[i].handle;
        }
    }

    /* 缓存未命中,调用系统调用查找 */
    handle_t h = sys_handle_find(name);
    if (h == HANDLE_INVALID) {
        return HANDLE_INVALID;
    }

    /* 添加到缓存 */
    if (g_cache_size < 16) {
        size_t name_len = strlen(name);
        if (name_len >= 32) {
            name_len = 31;
        }
        memcpy(g_handle_cache[g_cache_size].name, name, name_len);
        g_handle_cache[g_cache_size].name[name_len] = '\0';
        g_handle_cache[g_cache_size].handle         = (uint32_t)h;
        g_handle_cache[g_cache_size].valid          = true;
        g_cache_size++;
    }

    return (uint32_t)h;
}

void env_set_name(const char *name) {
    size_t len = strlen(name);
    if (len >= sizeof(g_env_name)) {
        len = sizeof(g_env_name) - 1;
    }
    memcpy(g_env_name, name, len);
    g_env_name[len] = '\0';
}

uint32_t env_require(const char *name) {
    uint32_t h = env_get_handle(name);
    if (h == HANDLE_INVALID) {
        printf("%s: required handle '%s' not found\n", g_env_name, name);
    }
    return h;
}

void *env_mmap_resource(const char *name, uint32_t *out_size) {
    handle_t h = sys_handle_find(name);
    if (h == HANDLE_INVALID) {
        printf("%s: handle '%s' not found\n", g_env_name, name);
        return NULL;
    }

    uint32_t size = 0;
    void    *addr = sys_mmap_phys(h, 0, 0, 0x03, &size);
    if (!addr || (intptr_t)addr < 0) {
        printf("%s: failed to map '%s'\n", g_env_name, name);
        return NULL;
    }

    if (out_size) {
        *out_size = size;
    }
    return addr;
}
