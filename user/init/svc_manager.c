/**
 * @file svc_manager.c
 * @brief 声明式服务管理器实现
 */

#include "svc_manager.h"

#include "ini_parser.h"

#include <stdio.h>
#include <string.h>
#include <xnix/abi/capability.h>
#include <xnix/udm/vfs.h>

/* 全局 tick 计数器(由 main.c 更新) */
static uint32_t g_ticks = 0;

uint32_t svc_get_ticks(void) {
    return g_ticks;
}

void svc_update_ticks(uint32_t ticks) {
    g_ticks = ticks;
}

void svc_manager_init(struct svc_manager *mgr) {
    memset(mgr, 0, sizeof(*mgr));

    /* 确保 /run 目录存在 */
    sys_mkdir(SVC_READY_DIR);
}

/**
 * 解析 section 名获取服务名
 * 格式: "service.name" -> "name"
 */
static bool parse_service_section(const char *section, char *name, size_t name_size) {
    const char *prefix     = "service.";
    size_t      prefix_len = strlen(prefix);

    if (strncmp(section, prefix, prefix_len) != 0) {
        return false;
    }

    const char *svc_name = section + prefix_len;
    size_t      len      = strlen(svc_name);
    if (len == 0 || len >= name_size) {
        return false;
    }

    memcpy(name, svc_name, len);
    name[len] = '\0';
    return true;
}

/**
 * 解析空格分隔的依赖列表
 */
static int parse_dep_list(const char *value, char deps[][SVC_NAME_MAX], int max_deps) {
    int         count = 0;
    const char *p     = value;

    while (*p && count < max_deps) {
        /* 跳过空格 */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* 提取依赖名 */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }

        size_t len = (size_t)(p - start);
        if (len > 0 && len < SVC_NAME_MAX) {
            memcpy(deps[count], start, len);
            deps[count][len] = '\0';
            count++;
        }
    }

    return count;
}

/**
 * 解析 capability 描述
 * 格式: "src:rights:dst"
 */
static bool parse_cap_desc(const char *value, struct svc_cap_desc *desc) {
    /* 简单解析 src:rights:dst 格式 */
    char   buf[64];
    size_t len = strlen(value);
    if (len >= sizeof(buf)) {
        return false;
    }
    memcpy(buf, value, len);
    buf[len] = '\0';

    char *p1 = strchr(buf, ':');
    if (!p1) {
        return false;
    }
    *p1++ = '\0';

    char *p2 = strchr(p1, ':');
    if (!p2) {
        return false;
    }
    *p2++ = '\0';

    /* 解析数值 */
    desc->src_handle = 0;
    desc->rights     = 0;
    desc->dst_hint   = 0;

    for (const char *s = buf; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            desc->src_handle = desc->src_handle * 10 + (*s - '0');
        }
    }

    for (const char *s = p1; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            desc->rights = desc->rights * 10 + (*s - '0');
        }
    }

    for (const char *s = p2; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            desc->dst_hint = desc->dst_hint * 10 + (*s - '0');
        }
    }

    return true;
}

/**
 * INI 解析上下文
 */
struct ini_ctx {
    struct svc_manager *mgr;
    struct svc_config  *current; /* 当前正在解析的服务 */
};

/**
 * INI 解析回调
 */
static bool ini_handler(const char *section, const char *key, const char *value, void *ctx) {
    struct ini_ctx     *ictx = (struct ini_ctx *)ctx;
    struct svc_manager *mgr  = ictx->mgr;

    /* 跳过 [general] 等非服务 section */
    char svc_name[SVC_NAME_MAX];
    if (!parse_service_section(section, svc_name, sizeof(svc_name))) {
        ictx->current = NULL;
        return true;
    }

    /* 查找或创建服务配置 */
    if (ictx->current == NULL || strcmp(ictx->current->name, svc_name) != 0) {
        int idx = svc_find_by_name(mgr, svc_name);
        if (idx < 0) {
            /* 新服务 */
            if (mgr->count >= SVC_MAX_SERVICES) {
                printf("[svc] Too many services\n");
                return true;
            }
            idx                    = mgr->count++;
            struct svc_config *cfg = &mgr->configs[idx];
            memset(cfg, 0, sizeof(*cfg));
            memcpy(cfg->name, svc_name, strlen(svc_name));
            cfg->name[strlen(svc_name)] = '\0';
            cfg->type                   = SVC_TYPE_MODULE;
        }
        ictx->current = &mgr->configs[idx];
    }

    struct svc_config *cfg = ictx->current;

    /* 解析键值 */
    if (strcmp(key, "builtin") == 0) {
        cfg->builtin = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "type") == 0) {
        if (strcmp(value, "module") == 0) {
            cfg->type = SVC_TYPE_MODULE;
        } else if (strcmp(value, "path") == 0) {
            cfg->type = SVC_TYPE_PATH;
        }
    } else if (strcmp(key, "module") == 0) {
        cfg->module_index = 0;
        for (const char *s = value; *s; s++) {
            if (*s >= '0' && *s <= '9') {
                cfg->module_index = cfg->module_index * 10 + (*s - '0');
            }
        }
    } else if (strcmp(key, "path") == 0) {
        size_t len = strlen(value);
        if (len >= SVC_PATH_MAX) {
            len = SVC_PATH_MAX - 1;
        }
        memcpy(cfg->path, value, len);
        cfg->path[len] = '\0';
    } else if (strcmp(key, "after") == 0) {
        cfg->after_count = parse_dep_list(value, cfg->after, SVC_DEPS_MAX);
    } else if (strcmp(key, "ready") == 0) {
        cfg->ready_count = parse_dep_list(value, cfg->ready, SVC_DEPS_MAX);
    } else if (strcmp(key, "wait_path") == 0) {
        size_t len = strlen(value);
        if (len >= SVC_PATH_MAX) {
            len = SVC_PATH_MAX - 1;
        }
        memcpy(cfg->wait_path, value, len);
        cfg->wait_path[len] = '\0';
    } else if (strcmp(key, "delay") == 0) {
        cfg->delay_ms = 0;
        for (const char *s = value; *s; s++) {
            if (*s >= '0' && *s <= '9') {
                cfg->delay_ms = cfg->delay_ms * 10 + (*s - '0');
            }
        }
    } else if (strcmp(key, "respawn") == 0) {
        cfg->respawn = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    } else if (strcmp(key, "caps") == 0) {
        if (cfg->cap_count < SVC_CAPS_MAX) {
            if (parse_cap_desc(value, &cfg->caps[cfg->cap_count])) {
                cfg->cap_count++;
            }
        }
    } else if (strcmp(key, "name") == 0) {
        /* name 字段可选,用于覆盖 section 中的名称 */
        size_t len = strlen(value);
        if (len >= SVC_NAME_MAX) {
            len = SVC_NAME_MAX - 1;
        }
        memcpy(cfg->name, value, len);
        cfg->name[len] = '\0';
    }

    return true;
}

int svc_load_config(struct svc_manager *mgr, const char *path) {
    struct ini_ctx ctx = {
        .mgr     = mgr,
        .current = NULL,
    };

    int ret = ini_parse_file(path, ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    printf("[svc] Loaded %d services from %s\n", mgr->count, path);
    return 0;
}

int svc_find_by_name(struct svc_manager *mgr, const char *name) {
    for (int i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->configs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void svc_mark_builtin(struct svc_manager *mgr, const char *name, int pid) {
    int idx = svc_find_by_name(mgr, name);
    if (idx < 0) {
        /* 服务不在配置文件中,添加一个临时条目 */
        if (mgr->count >= SVC_MAX_SERVICES) {
            return;
        }
        idx                    = mgr->count++;
        struct svc_config *cfg = &mgr->configs[idx];
        memset(cfg, 0, sizeof(*cfg));
        size_t len = strlen(name);
        if (len >= SVC_NAME_MAX) {
            len = SVC_NAME_MAX - 1;
        }
        memcpy(cfg->name, name, len);
        cfg->name[len] = '\0';
        cfg->builtin   = true;
    }

    mgr->runtime[idx].state = SVC_STATE_RUNNING;
    mgr->runtime[idx].pid   = pid;
    mgr->runtime[idx].ready = true; /* 内置服务标记为已就绪 */
}

bool svc_check_ready_file(const char *name) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.ready", SVC_READY_DIR, name);

    struct vfs_info info;
    return sys_info(path, &info) == 0;
}

bool svc_can_start(struct svc_manager *mgr, int idx) {
    struct svc_config *cfg = &mgr->configs[idx];

    /* 检查 after 依赖(服务已启动) */
    for (int i = 0; i < cfg->after_count; i++) {
        int dep = svc_find_by_name(mgr, cfg->after[i]);
        if (dep < 0) {
            /* 依赖的服务不存在 */
            return false;
        }
        if (mgr->runtime[dep].state < SVC_STATE_STARTING) {
            return false;
        }
    }

    /* 检查 ready 依赖(服务已就绪) */
    for (int i = 0; i < cfg->ready_count; i++) {
        int dep = svc_find_by_name(mgr, cfg->ready[i]);
        if (dep < 0) {
            return false;
        }
        if (!mgr->runtime[dep].ready) {
            return false;
        }
    }

    /* 检查 wait_path */
    if (cfg->wait_path[0]) {
        struct vfs_info info;
        if (sys_info(cfg->wait_path, &info) < 0) {
            return false;
        }
    }

    return true;
}

int svc_start_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    printf("[svc] Starting %s...\n", cfg->name);

    rt->state = SVC_STATE_STARTING;

    struct spawn_args args;
    memset(&args, 0, sizeof(args));

    /* 复制名称 */
    size_t name_len = strlen(cfg->name);
    if (name_len >= ABI_SPAWN_NAME_LEN) {
        name_len = ABI_SPAWN_NAME_LEN - 1;
    }
    memcpy(args.name, cfg->name, name_len);
    args.name[name_len] = '\0';

    args.module_index = cfg->module_index;
    args.cap_count    = (uint32_t)cfg->cap_count;

    /* 复制 capability */
    for (int i = 0; i < cfg->cap_count; i++) {
        args.caps[i].src      = cfg->caps[i].src_handle;
        args.caps[i].rights   = cfg->caps[i].rights;
        args.caps[i].dst_hint = cfg->caps[i].dst_hint;
    }

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[svc] Failed to start %s: %d\n", cfg->name, pid);
        rt->state = SVC_STATE_FAILED;
        return pid;
    }

    printf("[svc] %s started (pid=%d)\n", cfg->name, pid);
    rt->state = SVC_STATE_RUNNING;
    rt->pid   = pid;
    rt->ready = false;

    return pid;
}

void svc_tick(struct svc_manager *mgr) {
    /* 更新 tick 计数 */
    g_ticks += 100; /* 假设每 100ms 调用一次 */

    /* 检查 ready 文件(跳过内置服务,它们在配置加载时已就绪) */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        /* 内置服务不检查 ready 文件 */
        if (cfg->builtin) {
            continue;
        }

        if (rt->state == SVC_STATE_RUNNING && !rt->ready) {
            if (svc_check_ready_file(cfg->name)) {
                rt->ready = true;
                printf("[svc] %s is ready\n", cfg->name);
            }
        }
    }

    /* 尝试启动可启动的服务 */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        if (rt->state != SVC_STATE_PENDING) {
            continue;
        }
        if (cfg->builtin) {
            continue; /* 内置服务不由 tick 启动 */
        }

        if (svc_can_start(mgr, i)) {
            if (cfg->delay_ms > 0) {
                rt->state       = SVC_STATE_WAITING;
                rt->delay_start = g_ticks;
            } else {
                svc_start_service(mgr, i);
            }
        }
    }

    /* 处理延时等待 */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_runtime *rt = &mgr->runtime[i];
        if (rt->state == SVC_STATE_WAITING) {
            uint32_t elapsed = g_ticks - rt->delay_start;
            if (elapsed >= mgr->configs[i].delay_ms) {
                svc_start_service(mgr, i);
            }
        }
    }
}

void svc_handle_exit(struct svc_manager *mgr, int pid, int status) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_runtime *rt = &mgr->runtime[i];
        if (rt->pid == pid) {
            struct svc_config *cfg = &mgr->configs[i];
            printf("[svc] %s exited (status=%d)\n", cfg->name, status);

            rt->state = SVC_STATE_STOPPED;
            rt->pid   = -1;
            rt->ready = false;

            /* 删除 ready 文件 */
            char ready_path[64];
            snprintf(ready_path, sizeof(ready_path), "%s/%s.ready", SVC_READY_DIR, cfg->name);
            sys_del(ready_path);

            /* 检查是否需要重启 */
            if (cfg->respawn) {
                printf("[svc] Respawning %s...\n", cfg->name);
                rt->state = SVC_STATE_PENDING;
            }

            return;
        }
    }
}
