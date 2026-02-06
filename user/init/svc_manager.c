/**
 * @file svc_manager.c
 * @brief 声明式服务管理器实现
 */

#include "svc_manager.h"

#include <string.h>

void svc_manager_init(struct svc_manager *mgr) {
    memset(mgr, 0, sizeof(*mgr));
}

int svc_find_by_name(struct svc_manager *mgr, const char *name) {
    for (int i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->configs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

