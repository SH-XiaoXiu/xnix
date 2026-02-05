/**
 * @file svc_manager.c
 * @brief 声明式服务管理器实现
 */

#include "svc_manager.h"

#include "ini_parser.h"

#include <d/protocol/vfs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/process.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

/* 全局 tick 计数器 */
static uint32_t g_ticks = 0;

static bool parse_handle_section(const char *section, char *name, size_t name_size) {
    const char *prefix     = "handle.";
    size_t      prefix_len = strlen(prefix);

    if (strncmp(section, prefix, prefix_len) != 0) {
        return false;
    }

    const char *handle_name = section + prefix_len;
    size_t      len         = strlen(handle_name);
    if (len == 0 || len >= name_size) {
        return false;
    }

    memcpy(name, handle_name, len);
    name[len] = '\0';
    return true;
}

static bool parse_profile_section(const char *section, char *name, size_t name_size) {
    const char *prefix     = "profile.";
    size_t      prefix_len = strlen(prefix);

    if (strncmp(section, prefix, prefix_len) != 0) {
        return false;
    }

    const char *profile_name = section + prefix_len;
    size_t      len          = strlen(profile_name);
    if (len == 0 || len >= name_size) {
        return false;
    }

    memcpy(name, profile_name, len);
    name[len] = '\0';
    return true;
}

static struct svc_handle_def *handle_def_find(struct svc_manager *mgr, const char *name) {
    for (int i = 0; i < mgr->handle_def_count; i++) {
        if (strcmp(mgr->handle_defs[i].name, name) == 0) {
            return &mgr->handle_defs[i];
        }
    }
    return NULL;
}

static struct svc_handle_def *handle_def_get_or_add(struct svc_manager *mgr, const char *name) {
    struct svc_handle_def *def = handle_def_find(mgr, name);
    if (def) {
        return def;
    }

    if (mgr->handle_def_count >= SVC_MAX_HANDLE_DEFS) {
        return NULL;
    }

    def = &mgr->handle_defs[mgr->handle_def_count++];
    memset(def, 0, sizeof(*def));
    snprintf(def->name, sizeof(def->name), "%s", name);
    def->type   = SVC_HANDLE_TYPE_NONE;
    def->handle = HANDLE_INVALID;
    return def;
}

static int handle_def_create(struct svc_handle_def *def) {
    if (def->created) {
        return 0;
    }

    int h = -1;
    switch (def->type) {
    case SVC_HANDLE_TYPE_ENDPOINT:
        h = sys_endpoint_create(def->name);
        break;
    default:
        return -1;
    }

    if (h < 0) {
        return h;
    }

    def->handle  = (uint32_t)h;
    def->created = true;
    return 0;
}

static int handle_get_or_create(struct svc_manager *mgr, const char *name, uint32_t *out_handle) {
    struct svc_handle_def *def = handle_def_find(mgr, name);
    if (!def) {
        return -1;
    }
    if (handle_def_create(def) < 0) {
        return -1;
    }
    *out_handle = def->handle;
    return 0;
}

uint32_t svc_get_ticks(void) {
    return g_ticks;
}

void svc_manager_init(struct svc_manager *mgr) {
    memset(mgr, 0, sizeof(*mgr));
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

int svc_parse_handles(struct svc_manager *mgr, const char *handles_str,
                      struct svc_handle_desc *handles, int max_handles) {
    int         count = 0;
    const char *p     = handles_str;

    while (*p && count < max_handles) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        char spec[64];
        int  spec_len = 0;
        while (*p && *p != ' ' && *p != '\t' && spec_len < 63) {
            spec[spec_len++] = *p++;
        }
        spec[spec_len] = '\0';

        snprintf(handles[count].name, sizeof(handles[count].name), "%s", spec);
        handles[count].src_handle = HANDLE_INVALID;

        struct svc_handle_def *def = handle_def_get_or_add(mgr, spec);
        if (def && def->type == SVC_HANDLE_TYPE_NONE) {
            def->type = SVC_HANDLE_TYPE_ENDPOINT;
        }

        count++;
    }

    return count;
}

/**
 * INI 解析上下文
 */
struct ini_ctx {
    struct svc_manager    *mgr;
    struct svc_config     *current; /* 当前正在解析的服务 */
    struct svc_handle_def *current_handle;
    struct svc_profile    *current_profile; /* 当前正在解析的 profile */
};

static void svc_resolve_handles(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config *cfg = &mgr->configs[i];

        for (int j = 0; j < cfg->handle_count; j++) {
            struct svc_handle_desc *h = &cfg->handles[j];
            if (h->name[0] == '\0') {
                continue;
            }

            if (h->src_handle == HANDLE_INVALID) {
                uint32_t resolved = HANDLE_INVALID;
                if (handle_get_or_create(mgr, h->name, &resolved) < 0) {
                    printf("Unknown handle: %s\n", h->name);
                    continue;
                }
                h->src_handle = resolved;
            }
        }

        /* mount_ep 不在这里设置 - 将在服务启动后动态查找 */
    }
}

/**
 * INI 解析回调
 */
static bool ini_handler(const char *section, const char *key, const char *value, void *ctx) {
    struct ini_ctx     *ictx = (struct ini_ctx *)ctx;
    struct svc_manager *mgr  = ictx->mgr;

    char svc_name[SVC_NAME_MAX];
    if (parse_service_section(section, svc_name, sizeof(svc_name))) {
        if (ictx->current == NULL || strcmp(ictx->current->name, svc_name) != 0) {
            int idx = svc_find_by_name(mgr, svc_name);
            if (idx < 0) {
                if (mgr->count >= SVC_MAX_SERVICES) {
                    printf("Too many services\n");
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

        if (strcmp(key, "type") == 0) {
            if (strcmp(value, "module") == 0) {
                cfg->type = SVC_TYPE_MODULE;
            } else if (strcmp(value, "path") == 0) {
                cfg->type = SVC_TYPE_PATH;
            }
        } else if (strcmp(key, "module_name") == 0) {
            size_t len = strlen(value);
            if (len >= SVC_NAME_MAX) {
                len = SVC_NAME_MAX - 1;
            }
            memcpy(cfg->module_name, value, len);
            cfg->module_name[len] = '\0';
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
        } else if (strcmp(key, "builtin") == 0) {
            cfg->builtin = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "respawn") == 0) {
            cfg->respawn = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "no_ready_file") == 0) {
            cfg->no_ready_file = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "handles") == 0) {
            cfg->handle_count = svc_parse_handles(mgr, value, cfg->handles, SVC_HANDLES_MAX);
        } else if (strcmp(key, "mount") == 0) {
            size_t len = strlen(value);
            if (len >= SVC_PATH_MAX) {
                len = SVC_PATH_MAX - 1;
            }
            memcpy(cfg->mount, value, len);
            cfg->mount[len] = '\0';
        } else if (strcmp(key, "profile") == 0) {
            size_t len = strlen(value);
            if (len >= sizeof(cfg->profile)) {
                len = sizeof(cfg->profile) - 1;
            }
            memcpy(cfg->profile, value, len);
            cfg->profile[len] = '\0';
        } else if (strcmp(key, "provides") == 0) {
            /* 解析 provides 到 graph 节点 */
            int idx = svc_find_by_name(mgr, cfg->name);
            if (idx >= 0) {
                struct svc_graph_node *node = &mgr->graph[idx];
                node->provides_count        = parse_dep_list(value, node->provides, SVC_DEPS_MAX);
            }
        } else if (strcmp(key, "requires") == 0) {
            /* 解析 requires 到 graph 节点 */
            int idx = svc_find_by_name(mgr, cfg->name);
            if (idx >= 0) {
                struct svc_graph_node *node = &mgr->graph[idx];
                node->requires_count        = parse_dep_list(value, node->requires, SVC_DEPS_MAX);
            }
        } else if (strcmp(key, "wants") == 0) {
            /* 解析 wants 到 graph 节点 */
            int idx = svc_find_by_name(mgr, cfg->name);
            if (idx >= 0) {
                struct svc_graph_node *node = &mgr->graph[idx];
                node->wants_count           = parse_dep_list(value, node->wants, SVC_DEPS_MAX);
            }
        } else if (key[0] == 'x' && strncmp(key, "xnix.", 5) == 0) {
            /* 权限覆盖:xnix.xxx = true/false */
            if (cfg->perm_count < 8) {
                snprintf(cfg->perms[cfg->perm_count], 64, "%s=%s", key, value);
                cfg->perm_count++;
            }
        }

        return true;
    }

    char handle_name[SVC_HANDLE_NAME_MAX];
    if (parse_handle_section(section, handle_name, sizeof(handle_name))) {
        ictx->current         = NULL;
        ictx->current_profile = NULL;

        if (ictx->current_handle == NULL || strcmp(ictx->current_handle->name, handle_name) != 0) {
            ictx->current_handle = handle_def_get_or_add(mgr, handle_name);
        }

        struct svc_handle_def *h = ictx->current_handle;
        if (!h) {
            printf("Too many handle defs\n");
            return true;
        }

        if (strcmp(key, "type") == 0) {
            if (strcmp(value, "endpoint") == 0) {
                h->type = SVC_HANDLE_TYPE_ENDPOINT;
            }
        }
        return true;
    }

    /* 处理 [profile.xxx] */
    char profile_name[32];
    if (parse_profile_section(section, profile_name, sizeof(profile_name))) {
        ictx->current        = NULL;
        ictx->current_handle = NULL;

        /* 查找或创建 profile */
        if (ictx->current_profile == NULL ||
            strcmp(ictx->current_profile->name, profile_name) != 0) {
            struct svc_profile *prof = NULL;
            for (int i = 0; i < mgr->profile_count; i++) {
                if (strcmp(mgr->profiles[i].name, profile_name) == 0) {
                    prof = &mgr->profiles[i];
                    break;
                }
            }

            if (!prof && mgr->profile_count < SVC_MAX_PROFILES) {
                prof = &mgr->profiles[mgr->profile_count++];
                memset(prof, 0, sizeof(*prof));
                strncpy(prof->name, profile_name, sizeof(prof->name) - 1);
            }

            ictx->current_profile = prof;
        }

        struct svc_profile *prof = ictx->current_profile;
        if (!prof) {
            printf("Too many profiles\n");
            return true;
        }

        /* 解析 profile 字段 */
        if (strcmp(key, "inherit") == 0) {
            strncpy(prof->inherit, value, sizeof(prof->inherit) - 1);
        } else if (key[0] == 'x' && strncmp(key, "xnix.", 5) == 0) {
            /* 权限节点:xnix.xxx = true/false */
            if (prof->perm_count < SVC_PERM_NODES_MAX) {
                struct svc_perm_entry *perm = &prof->perms[prof->perm_count++];
                strncpy(perm->name, key, sizeof(perm->name) - 1);
                perm->value = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            }
        }

        return true;
    }

    ictx->current         = NULL;
    ictx->current_handle  = NULL;
    ictx->current_profile = NULL;

    return true;
}

int svc_load_config(struct svc_manager *mgr, const char *path) {
    struct ini_ctx ctx = {
        .mgr             = mgr,
        .current         = NULL,
        .current_handle  = NULL,
        .current_profile = NULL,
    };

    int ret = ini_parse_file(path, ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    /* 解析服务发现(provides/requires/wants) */
    if (svc_resolve_service_discovery(mgr) < 0) {
        printf("Failed to resolve service discovery\n");
        return -1;
    }

    svc_resolve_handles(mgr);

    /* builtin 服务保持 PENDING 状态,让调度器正常启动它们 */

    /* 构建依赖图 */
    if (svc_build_dependency_graph(mgr) < 0) {
        printf("Failed to build dependency graph\n");
        return -1;
    }

    printf("Loaded %d services from %s\n", mgr->count, path);
    return 0;
}

int svc_load_config_string(struct svc_manager *mgr, const char *content) {
    struct ini_ctx ctx = {
        .mgr             = mgr,
        .current         = NULL,
        .current_handle  = NULL,
        .current_profile = NULL,
    };

    int ret = ini_parse_buffer(content, strlen(content), ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    /* 解析服务发现(provides/requires/wants) */
    if (svc_resolve_service_discovery(mgr) < 0) {
        printf("Failed to resolve service discovery\n");
        return -1;
    }

    svc_resolve_handles(mgr);

    /* builtin 服务保持 PENDING 状态,让调度器正常启动它们 */

    /* 构建依赖图 */
    if (svc_build_dependency_graph(mgr) < 0) {
        printf("Failed to build dependency graph\n");
        return -1;
    }

    printf("Loaded %d services from embedded config\n", mgr->count);
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

bool svc_check_ready_file(const char *name) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s.ready", SVC_READY_DIR, name);

    struct vfs_stat st;
    return vfs_stat(path, &st) == 0;
}

bool svc_can_start(struct svc_manager *mgr, int idx) {
    struct svc_config *cfg = &mgr->configs[idx];

    /* 检查 after 依赖(服务已启动) */
    for (int i = 0; i < cfg->after_count; i++) {
        int dep = svc_find_by_name(mgr, cfg->after[i]);
        if (dep < 0) {
            /* 依赖的服务不存在,跳过这个依赖 */
            continue;
        }
        if (mgr->runtime[dep].state < SVC_STATE_STARTING) {
            return false;
        }
    }

    /* 检查 ready 依赖(服务已就绪) */
    for (int i = 0; i < cfg->ready_count; i++) {
        int dep = svc_find_by_name(mgr, cfg->ready[i]);
        if (dep < 0) {
            continue;
        }
        if (!mgr->runtime[dep].ready) {
            return false;
        }
    }

    /* 检查 wait_path */
    if (cfg->wait_path[0]) {
        struct vfs_stat st;
        if (vfs_stat(cfg->wait_path, &st) < 0) {
            return false;
        }
    }

    return true;
}

/**
 * 探测文件系统驱动是否就绪(通过发送 IPC 探测消息)
 */
static bool probe_fs_ready(uint32_t ep, uint32_t timeout_ms) {
    uint32_t       elapsed        = 0;
    const uint32_t probe_interval = 50;

    while (elapsed < timeout_ms) {
        struct ipc_message msg   = {0};
        struct ipc_message reply = {0};

        /* 发送 STAT 探测消息(对根目录) */
        msg.regs.data[0]      = UDM_VFS_INFO;
        const char *test_path = ".";
        msg.buffer.data       = (void *)test_path;
        msg.buffer.size       = 2;

        int ret = sys_ipc_call(ep, &msg, &reply, 500);
        if (ret == 0) {
            /* 驱动响应了(即使是错误),说明已就绪 */
            printf("  Probe succeeded!\n");
            return true;
        }

        if (elapsed < 200) {
            printf("  Probe attempt (elapsed=%ums) failed: ret=%d\n", elapsed, ret);
        }

        msleep(probe_interval);
        elapsed += probe_interval;
    }

    return false;
}

/**
 * 执行挂载操作
 */
static int do_mount(struct svc_config *cfg) {
    if (cfg->mount[0] == '\0') {
        return 0;
    }

    printf("Mounting %s on %s (ep=%u)\n", cfg->name, cfg->mount, cfg->mount_ep);
    int ret = vfs_mount(cfg->mount, cfg->mount_ep);
    if (ret < 0) {
        printf("Failed to mount %s: %d\n", cfg->mount, ret);
    }
    return ret;
}

int svc_start_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    /* 启动早期服务时使用早期控制台 */
    extern void early_puts(const char *);
    extern bool early_console_is_active(void);
    if (early_console_is_active()) {
        early_puts("[INIT] starting ");
        early_puts(cfg->name);
        early_puts("\n");
    } else {
        printf("Starting %s...\n", cfg->name);
    }

    rt->state = SVC_STATE_STARTING;

    int pid;

    if (cfg->type == SVC_TYPE_PATH) {
        /* 从文件系统加载 ELF */
        struct abi_exec_args exec_args;
        memset(&exec_args, 0, sizeof(exec_args));

        size_t path_len = strlen(cfg->path);
        if (path_len >= ABI_EXEC_PATH_MAX) {
            path_len = ABI_EXEC_PATH_MAX - 1;
        }
        memcpy(exec_args.path, cfg->path, path_len);
        exec_args.path[path_len] = '\0';

        if (cfg->profile[0] != '\0') {
            size_t profile_len = strlen(cfg->profile);
            if (profile_len >= ABI_SPAWN_PROFILE_LEN) {
                profile_len = ABI_SPAWN_PROFILE_LEN - 1;
            }
            memcpy(exec_args.profile_name, cfg->profile, profile_len);
            exec_args.profile_name[profile_len] = '\0';
        } else {
            exec_args.profile_name[0] = '\0';
        }

        exec_args.argc  = 0;
        exec_args.flags = 0;

        /* 传递 handles */
        exec_args.handle_count = (uint32_t)cfg->handle_count;
        for (int i = 0; i < cfg->handle_count && i < ABI_EXEC_MAX_HANDLES; i++) {
            exec_args.handles[i].src = cfg->handles[i].src_handle;
            snprintf(exec_args.handles[i].name, sizeof(exec_args.handles[i].name), "%s",
                     cfg->handles[i].name);
        }

        pid = sys_exec(&exec_args);
    } else {
        /* 从 Multiboot module 加载 */
        struct spawn_args args;
        memset(&args, 0, sizeof(args));

        size_t name_len = strlen(cfg->name);
        if (name_len >= ABI_SPAWN_NAME_LEN) {
            name_len = ABI_SPAWN_NAME_LEN - 1;
        }
        memcpy(args.name, cfg->name, name_len);
        args.name[name_len] = '\0';

        /* 设置权限 profile */
        if (cfg->profile[0] != '\0') {
            size_t profile_len = strlen(cfg->profile);
            if (profile_len >= ABI_SPAWN_PROFILE_LEN) {
                profile_len = ABI_SPAWN_PROFILE_LEN - 1;
            }
            memcpy(args.profile_name, cfg->profile, profile_len);
            args.profile_name[profile_len] = '\0';
        } else {
            args.profile_name[0] = '\0';
        }

        memcpy(args.module_name, cfg->module_name, sizeof(args.module_name));
        args.handle_count = (uint32_t)cfg->handle_count;

        for (int i = 0; i < cfg->handle_count; i++) {
            args.handles[i].src = cfg->handles[i].src_handle;
            snprintf(args.handles[i].name, sizeof(args.handles[i].name), "%s",
                     cfg->handles[i].name);
        }

        pid = sys_spawn(&args);
    }

    if (pid < 0) {
        /* 错误输出使用早期控制台 */
        extern void early_puts(const char *);
        extern bool early_console_is_active(void);
        if (early_console_is_active()) {
            early_puts("[INIT] ERROR: failed to start ");
            early_puts(cfg->name);
            early_puts("\n");
        } else {
            printf("Failed to start %s: %d\n", cfg->name, pid);
        }
        rt->state = SVC_STATE_FAILED;
        return pid;
    }

    /* Success - show PID */
    if (early_console_is_active()) {
        early_puts("[INIT] started ");
        early_puts(cfg->name);
        char pidbuf[16];
        snprintf(pidbuf, sizeof(pidbuf), " (pid %d)\n", pid);
        early_puts(pidbuf);
    } else {
        printf("%s started (pid=%d)\n", cfg->name, pid);
    }

    rt->state = SVC_STATE_RUNNING;
    rt->pid   = pid;

    if (cfg->no_ready_file) {
        rt->ready = true;
    } else {
        rt->ready = false;
    }

    /* 核心服务(有 mount 的)需要同步等待就绪并挂载 */
    if (cfg->mount[0]) {
        /* 给服务时间初始化并进入接收循环 */
        /* 让出 CPU 确保新进程能被调度执行 */
        for (int i = 0; i < 5; i++) {
            msleep(20);
        }

        /* 查找服务提供的 endpoint - 服务应该已经创建并注册了它 */
        int svc_idx = -1;
        for (int k = 0; k < mgr->count; k++) {
            if (&mgr->configs[k] == cfg) {
                svc_idx = k;
                break;
            }
        }

        if (svc_idx >= 0 && mgr->graph[svc_idx].provides_count > 0) {
            /* 服务提供的 endpoint 应该是第一个 handle (在 svc_resolve_service_discovery 中添加) */
            if (cfg->handle_count > 0) {
                const char *ep_name = mgr->graph[svc_idx].provides[0];
                cfg->mount_ep       = cfg->handles[0].src_handle;

                if (cfg->mount_ep == HANDLE_INVALID) {
                    printf("ERROR: Service '%s' mount_ep is INVALID\n", cfg->name);
                    rt->state = SVC_STATE_FAILED;
                    return pid;
                }

                /* 文件系统驱动不能使用 VFS 客户端(因为它们自己就是文件系统),
                 * 所以通过 IPC 探测其就绪状态 */
                printf("Probing %s readiness (ep=%u for '%s')...\n", cfg->name, cfg->mount_ep,
                       ep_name);
                if (probe_fs_ready(cfg->mount_ep, 5000)) {
                    int mount_ret = do_mount(cfg);
                    if (mount_ret < 0) {
                        rt->state = SVC_STATE_FAILED;
                        return pid;
                    }
                    rt->ready = true;
                    printf("%s mounted on %s\n", cfg->name, cfg->mount);
                } else {
                    printf("Timeout: %s did not respond to probes\n", cfg->name);
                    rt->state = SVC_STATE_FAILED;
                    return pid;
                }
            } else {
                printf("ERROR: Service '%s' provides endpoint but has no handles\n", cfg->name);
                rt->state = SVC_STATE_FAILED;
                return pid;
            }
        }
    }

    return pid;
}

void svc_tick(struct svc_manager *mgr) {
    /* 更新 tick 计数 */
    g_ticks += 50; /* 假设每 50ms 调用一次 */

    /* 检查 ready 文件 */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        if (rt->state == SVC_STATE_RUNNING && !rt->ready) {
            if (svc_check_ready_file(cfg->name)) {
                rt->ready = true;
                printf("%s is ready\n", cfg->name);
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

        /* builtin 服务也需要启动 */

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
            printf("%s exited (status=%d)\n", cfg->name, status);

            rt->state = SVC_STATE_STOPPED;
            rt->pid   = -1;
            rt->ready = false;

            /* 删除 ready 文件 */
            char ready_path[64];
            snprintf(ready_path, sizeof(ready_path), "%s/%s.ready", SVC_READY_DIR, cfg->name);
            vfs_delete(ready_path);

            /* 检查是否需要重启 */
            if (cfg->respawn) {
                printf("Respawning %s...\n", cfg->name);
                rt->state = SVC_STATE_PENDING;
            }

            return;
        }
    }
}

/**
 * DFS 循环依赖检测核心逻辑
 */
static bool svc_dfs_cycle_check(struct svc_manager *mgr, int idx, int *path, int path_len) {
    struct svc_graph_node *node = &mgr->graph[idx];

    if (node->in_path) {
        /* 发现循环,打印路径 */
        printf("ERROR: Circular dependency detected:\n  ");
        for (int i = 0; i < path_len; i++) {
            printf("%s -> ", mgr->configs[path[i]].name);
        }
        printf("%s\n", mgr->configs[idx].name);
        return true;
    }

    if (node->visited) {
        return false; /* 已访问过,无环 */
    }

    node->in_path  = true;
    path[path_len] = idx;

    /* 遍历所有依赖 */
    for (int i = 0; i < node->dep_count; i++) {
        int target = node->deps[i].target_idx;
        if (svc_dfs_cycle_check(mgr, target, path, path_len + 1)) {
            return true;
        }
    }

    node->in_path = false;
    node->visited = true;
    return false;
}

/**
 * 检测循环依赖
 */
static int svc_detect_cycles(struct svc_manager *mgr) {
    int path[SVC_MAX_SERVICES];

    /* 重置访问标记 */
    for (int i = 0; i < mgr->count; i++) {
        mgr->graph[i].visited = false;
        mgr->graph[i].in_path = false;
    }

    /* 对每个未访问节点执行 DFS */
    for (int i = 0; i < mgr->count; i++) {
        if (!mgr->graph[i].visited) {
            if (svc_dfs_cycle_check(mgr, i, path, 0)) {
                return -1;
            }
        }
    }

    return 0;
}

/**
 * 拓扑排序(Kahn 算法)
 *
 * 计算每个服务的拓扑层级,同层服务可并行启动
 */
static int svc_topological_sort(struct svc_manager *mgr) {
    int in_degree[SVC_MAX_SERVICES] = {0};
    int queue[SVC_MAX_SERVICES];
    int queue_front = 0, queue_back = 0;
    int level = 0;

    /* 计算入度 */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_graph_node *node = &mgr->graph[i];
        for (int j = 0; j < node->dep_count; j++) {
            in_degree[i]++;
        }
    }

    /* 入度为 0 的节点入队 */
    for (int i = 0; i < mgr->count; i++) {
        if (in_degree[i] == 0) {
            queue[queue_back++]      = i;
            mgr->graph[i].topo_level = 0;
        }
    }

    /* BFS 处理层级 */
    int processed = 0;
    while (queue_front < queue_back) {
        int level_size = queue_back - queue_front;

        for (int i = 0; i < level_size; i++) {
            int idx                      = queue[queue_front++];
            mgr->topo_order[processed++] = idx;

            /* 更新依赖于此服务的节点 */
            for (int j = 0; j < mgr->count; j++) {
                struct svc_graph_node *node = &mgr->graph[j];
                for (int k = 0; k < node->dep_count; k++) {
                    if (node->deps[k].target_idx == idx) {
                        in_degree[j]--;
                        if (in_degree[j] == 0) {
                            queue[queue_back++]      = j;
                            mgr->graph[j].topo_level = mgr->graph[idx].topo_level + 1;
                        }
                    }
                }
            }
        }
        level++;
    }

    if (processed != mgr->count) {
        printf("ERROR: Topological sort failed (cyclic dependency?)\n");
        return -1;
    }

    mgr->max_topo_level = level - 1;
    printf("Dependency graph validated: %d services, %d levels\n", mgr->count, level);
    return 0;
}

/**
 * 构建依赖图
 *
 * 从配置文件加载后调用,解析所有依赖关系并构建图
 */
int svc_build_dependency_graph(struct svc_manager *mgr) {
    /* 清空图节点的依赖和拓扑信息,但保留 provides/requires/wants */
    for (int i = 0; i < mgr->count; i++) {
        mgr->graph[i].dep_count    = 0;
        mgr->graph[i].topo_level   = 0;
        mgr->graph[i].pending_deps = 0;
        mgr->graph[i].visited      = false;
        mgr->graph[i].in_path      = false;
        /* 不清空 provides/requires/wants,它们在 INI 解析时填充 */
    }

    /* 解析依赖关系 */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config     *cfg  = &mgr->configs[i];
        struct svc_graph_node *node = &mgr->graph[i];

        /* 解析 after 依赖 */
        for (int j = 0; j < cfg->after_count; j++) {
            int dep_idx = svc_find_by_name(mgr, cfg->after[j]);
            if (dep_idx < 0) {
                printf("ERROR: Service '%s' depends on unknown service '%s' (after)\n", cfg->name,
                       cfg->after[j]);
                return -1; /* 硬错误:依赖不存在 */
            }

            if (node->dep_count >= SVC_DEPS_MAX * 3) {
                printf("ERROR: Service '%s' has too many dependencies\n", cfg->name);
                return -1;
            }

            node->deps[node->dep_count].target_idx = dep_idx;
            node->deps[node->dep_count].type       = DEP_AFTER;
            strncpy(node->deps[node->dep_count].name, cfg->after[j], SVC_NAME_MAX);
            node->dep_count++;
        }

        /* 解析 ready 依赖(转换为 REQUIRES) */
        for (int j = 0; j < cfg->ready_count; j++) {
            int dep_idx = svc_find_by_name(mgr, cfg->ready[j]);
            if (dep_idx < 0) {
                printf("ERROR: Service '%s' requires unknown service '%s' (ready)\n", cfg->name,
                       cfg->ready[j]);
                return -1;
            }

            if (node->dep_count >= SVC_DEPS_MAX * 3) {
                printf("ERROR: Service '%s' has too many dependencies\n", cfg->name);
                return -1;
            }

            node->deps[node->dep_count].target_idx = dep_idx;
            node->deps[node->dep_count].type       = DEP_REQUIRES;
            strncpy(node->deps[node->dep_count].name, cfg->ready[j], SVC_NAME_MAX);
            node->dep_count++;
        }
    }

    /* 检测循环依赖 */
    if (svc_detect_cycles(mgr) < 0) {
        return -1;
    }

    /* 拓扑排序 */
    if (svc_topological_sort(mgr) < 0) {
        return -1;
    }

    mgr->graph_valid = true;
    return 0;
}

/**
 * 高级依赖检查(支持 REQUIRES/WANTS/AFTER)
 */
static bool svc_can_start_advanced(struct svc_manager *mgr, int idx) {
    struct svc_config     *cfg  = &mgr->configs[idx];
    struct svc_graph_node *node = &mgr->graph[idx];

    /* 检查所有依赖 */
    for (int i = 0; i < node->dep_count; i++) {
        struct svc_dependency *dep       = &node->deps[i];
        int                    target    = dep->target_idx;
        struct svc_runtime    *target_rt = &mgr->runtime[target];

        switch (dep->type) {
        case DEP_REQUIRES:
            /* 硬依赖:必须运行且就绪 */
            if (target_rt->state < SVC_STATE_RUNNING || !target_rt->ready) {
                return false;
            }
            break;

        case DEP_WANTS:
            /* 软依赖:如果运行则等待就绪,失败则跳过 */
            if (target_rt->state == SVC_STATE_RUNNING && !target_rt->ready) {
                return false;
            }
            /* FAILED/STOPPED 状态:允许继续启动 */
            break;

        case DEP_AFTER:
            /* 顺序依赖:仅等待启动,不等就绪 */
            if (target_rt->state < SVC_STATE_STARTING) {
                return false;
            }
            break;
        }
    }

    /* 检查 wait_path */
    if (cfg->wait_path[0]) {
        /* VFS not available in current architecture, skip */
        return false;
    }

    return true;
}

/**
 * 处理延时等待(提取为独立函数)
 */
static void svc_process_delays(struct svc_manager *mgr) {
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

/**
 * 并行启动调度器
 *
 * 按拓扑层级并行启动服务
 */
void svc_tick_parallel(struct svc_manager *mgr) {
    g_ticks += 50;

    /* 处理延时等待 */
    svc_process_delays(mgr);

    /* 按层级启动 */
    for (int level = 0; level <= mgr->max_topo_level; level++) {
        bool level_has_pending = false;

        for (int i = 0; i < mgr->count; i++) {
            if (mgr->graph[i].topo_level != level) {
                continue;
            }

            struct svc_runtime *rt = &mgr->runtime[i];

            if (rt->state == SVC_STATE_PENDING) {
                /* builtin 服务也通过调度器启动 */

                if (svc_can_start_advanced(mgr, i)) {
                    if (mgr->configs[i].delay_ms > 0) {
                        rt->state       = SVC_STATE_WAITING;
                        rt->delay_start = g_ticks;
                    } else {
                        svc_start_service(mgr, i);
                    }
                } else {
                    level_has_pending = true;
                }
            } else if (rt->state < SVC_STATE_RUNNING) {
                level_has_pending = true;
            }
        }

        /* 本层有 pending,不处理下一层 */
        if (level_has_pending) {
            break;
        }
    }
}

/**
 * 处理服务就绪通知(IPC 消息)
 */
void svc_handle_ready_notification(struct svc_manager *mgr, struct ipc_message *msg) {
    if (msg->regs.data[0] != SVC_MSG_READY) {
        return;
    }

    struct svc_ready_msg *ready = (struct svc_ready_msg *)msg->buffer.data;
    int                   idx   = svc_find_by_name(mgr, ready->name);

    if (idx >= 0 && mgr->runtime[idx].state == SVC_STATE_RUNNING) {
        mgr->runtime[idx].ready = true;
        printf("[INIT] Service '%s' reported ready\n", ready->name);
    }
}

/**
 * 解析服务的 provides/requires/wants 并自动分配 handles
 *
 * 在配置加载后,构建依赖图时调用
 */
int svc_resolve_service_discovery(struct svc_manager *mgr) {
    /* 第一遍:收集所有 provides,init 创建 endpoints 并传递给提供服务的进程 */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config     *cfg  = &mgr->configs[i];
        struct svc_graph_node *node = &mgr->graph[i];

        for (int j = 0; j < node->provides_count; j++) {
            const char *ep_name = node->provides[j];

            printf("Service '%s' provides '%s'\n", cfg->name, ep_name);

            /* 确保 handle_def 存在 */
            struct svc_handle_def *def = handle_def_get_or_add(mgr, ep_name);
            if (!def) {
                printf("ERROR: Too many handle definitions\n");
                return -1;
            }

            if (def->type == SVC_HANDLE_TYPE_NONE) {
                def->type = SVC_HANDLE_TYPE_ENDPOINT;
            }

            /* 提供服务的进程需要接收 init 创建的 endpoint handle */
            if (cfg->handle_count >= SVC_HANDLES_MAX) {
                printf("ERROR: Service '%s' has too many handles\n", cfg->name);
                return -1;
            }

            struct svc_handle_desc *h = &cfg->handles[cfg->handle_count++];
            strncpy(h->name, ep_name, SVC_HANDLE_NAME_MAX);
            h->src_handle = HANDLE_INVALID; /* 将在 svc_resolve_handles 中创建 */

            printf("Service '%s' will receive its own handle '%s' (provides)\n", cfg->name,
                   ep_name);
        }
    }

    /* 第二遍:分配 requires/wants 的 handles */
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config     *cfg  = &mgr->configs[i];
        struct svc_graph_node *node = &mgr->graph[i];

        /* 处理 requires */
        for (int j = 0; j < node->requires_count; j++) {
            const char *ep_name = node->requires[j];

            printf("Service '%s' will receive handle '%s' (requires)\n", cfg->name, ep_name);

            /* 查找 handle 定义 */
            struct svc_handle_def *def = handle_def_find(mgr, ep_name);
            if (!def) {
                printf("ERROR: Service '%s' requires unknown handle '%s'\n", cfg->name, ep_name);
                return -1;
            }

            /* 添加到服务的 handle 列表 */
            if (cfg->handle_count >= SVC_HANDLES_MAX) {
                printf("ERROR: Service '%s' has too many handles\n", cfg->name);
                return -1;
            }

            struct svc_handle_desc *h = &cfg->handles[cfg->handle_count++];
            strncpy(h->name, ep_name, SVC_HANDLE_NAME_MAX);
            h->src_handle = HANDLE_INVALID; /* 稍后由 svc_resolve_handles 填充 */
        }

        /* 处理 wants */
        for (int j = 0; j < node->wants_count; j++) {
            const char *ep_name = node->wants[j];

            /* 查找 handle 定义(可选,不存在不报错) */
            struct svc_handle_def *def = handle_def_find(mgr, ep_name);
            if (!def) {
                printf("Service '%s' wants optional handle '%s' (not found, skipping)\n", cfg->name,
                       ep_name);
                continue;
            }

            /* 添加到服务的 handle 列表 */
            if (cfg->handle_count >= SVC_HANDLES_MAX) {
                printf("ERROR: Service '%s' has too many handles\n", cfg->name);
                return -1;
            }

            struct svc_handle_desc *h = &cfg->handles[cfg->handle_count++];
            strncpy(h->name, ep_name, SVC_HANDLE_NAME_MAX);
            h->src_handle = HANDLE_INVALID;
        }
    }

    return 0;
}
