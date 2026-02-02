/**
 * @file svc_manager.c
 * @brief 声明式服务管理器实现
 */

#include "svc_manager.h"

#include "ini_parser.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/capability.h>
#include <xnix/abi/process.h>
#include <xnix/udm/vfs.h>

/* 全局 tick 计数器 */
static uint32_t g_ticks = 0;

/**
 * Capability 名称到 handle 的映射
 * init 从内核继承这些 cap(顺序由内核决定)
 */
static const struct {
    const char *name;
    uint32_t    handle;
    uint32_t    rights;
} g_cap_map[] = {
    {"serial_ep", 0, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"ioport", 1, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"vfs_ep", 2, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"ata_io", 3, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"ata_ctrl", 4, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"fat_vfs_ep", 5, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"fb_ep", 6, CAP_READ | CAP_WRITE | CAP_GRANT},
    {"rootfs_ep", 7, CAP_READ | CAP_WRITE | CAP_GRANT},
    {NULL, 0, 0},
};

/**
 * 查找 capability 名称对应的 handle
 */
static int find_cap_handle(const char *name) {
    for (int i = 0; g_cap_map[i].name != NULL; i++) {
        if (strcmp(g_cap_map[i].name, name) == 0) {
            return (int)g_cap_map[i].handle;
        }
    }
    return -1;
}

/**
 * 获取 capability 的默认权限
 */
static uint32_t get_cap_rights(const char *name) {
    for (int i = 0; g_cap_map[i].name != NULL; i++) {
        if (strcmp(g_cap_map[i].name, name) == 0) {
            return g_cap_map[i].rights;
        }
    }
    return CAP_READ | CAP_WRITE;
}

uint32_t svc_get_ticks(void) {
    return g_ticks;
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
 * 解析 caps 字符串
 * 格式: "cap_name:dst_hint cap_name:dst_hint ..."
 */
int svc_parse_caps(const char *caps_str, struct svc_cap_desc *caps, int max_caps) {
    int         count = 0;
    const char *p     = caps_str;

    while (*p && count < max_caps) {
        /* 跳过空格 */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* 提取 cap_name:dst_hint */
        char cap_spec[64];
        int  spec_len = 0;
        while (*p && *p != ' ' && *p != '\t' && spec_len < 63) {
            cap_spec[spec_len++] = *p++;
        }
        cap_spec[spec_len] = '\0';

        /* 解析 name:dst_hint */
        char *colon = strchr(cap_spec, ':');
        if (colon) {
            *colon               = '\0';
            const char *cap_name = cap_spec;
            const char *dst_str  = colon + 1;

            int handle = find_cap_handle(cap_name);
            if (handle >= 0) {
                caps[count].src_handle = (uint32_t)handle;
                caps[count].rights     = get_cap_rights(cap_name);
                caps[count].dst_hint   = 0;

                /* 解析 dst_hint */
                for (const char *s = dst_str; *s; s++) {
                    if (*s >= '0' && *s <= '9') {
                        caps[count].dst_hint = caps[count].dst_hint * 10 + (*s - '0');
                    }
                }

                count++;
            } else {
                printf("[svc] Unknown capability: %s\n", cap_name);
            }
        }
    }

    return count;
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

    /* 跳过 [general] [core] 等非服务 section */
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
    if (strcmp(key, "type") == 0) {
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
        cfg->cap_count = svc_parse_caps(value, cfg->caps, SVC_CAPS_MAX);
    } else if (strcmp(key, "mount") == 0) {
        size_t len = strlen(value);
        if (len >= SVC_PATH_MAX) {
            len = SVC_PATH_MAX - 1;
        }
        memcpy(cfg->mount, value, len);
        cfg->mount[len] = '\0';

        /* mount 路径暗示了 endpoint handle(从 caps 中找) */
        if (cfg->cap_count > 0) {
            cfg->mount_ep = cfg->caps[0].src_handle;
        }
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

int svc_load_config_string(struct svc_manager *mgr, const char *content) {
    struct ini_ctx ctx = {
        .mgr     = mgr,
        .current = NULL,
    };

    int ret = ini_parse_buffer(content, strlen(content), ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    printf("[svc] Loaded %d services from embedded config\n", mgr->count);
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

    struct vfs_info info;
    return sys_info(path, &info) == 0;
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
        struct vfs_info info;
        if (sys_info(cfg->wait_path, &info) < 0) {
            return false;
        }
    }

    return true;
}

/**
 * 等待服务就绪(用于核心服务的同步启动)
 */
static bool wait_for_ready(const char *name, int timeout_ms) {
    int waited = 0;
    while (waited < timeout_ms) {
        if (svc_check_ready_file(name)) {
            return true;
        }
        msleep(10);
        waited += 10;
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

    printf("[svc] Mounting %s on %s (ep=%u)\n", cfg->name, cfg->mount, cfg->mount_ep);
    int ret = sys_mount(cfg->mount, cfg->mount_ep);
    if (ret < 0) {
        printf("[svc] Failed to mount %s: %d\n", cfg->mount, ret);
    }
    return ret;
}

int svc_start_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    printf("[svc] Starting %s...\n", cfg->name);

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

        exec_args.argc  = 0;
        exec_args.flags = 0;

        /* 传递 capabilities */
        exec_args.cap_count = (uint32_t)cfg->cap_count;
        for (int i = 0; i < cfg->cap_count && i < ABI_EXEC_MAX_CAPS; i++) {
            exec_args.caps[i].src      = cfg->caps[i].src_handle;
            exec_args.caps[i].rights   = cfg->caps[i].rights;
            exec_args.caps[i].dst_hint = cfg->caps[i].dst_hint;
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

        args.module_index = cfg->module_index;
        args.cap_count    = (uint32_t)cfg->cap_count;

        for (int i = 0; i < cfg->cap_count; i++) {
            args.caps[i].src      = cfg->caps[i].src_handle;
            args.caps[i].rights   = cfg->caps[i].rights;
            args.caps[i].dst_hint = cfg->caps[i].dst_hint;
        }

        pid = sys_spawn(&args);
    }

    if (pid < 0) {
        printf("[svc] Failed to start %s: %d\n", cfg->name, pid);
        rt->state = SVC_STATE_FAILED;
        return pid;
    }

    printf("[svc] %s started (pid=%d)\n", cfg->name, pid);
    rt->state = SVC_STATE_RUNNING;
    rt->pid   = pid;
    rt->ready = false;

    /* 核心服务(有 mount 的)需要同步等待就绪并挂载 */
    if (cfg->mount[0]) {
        /* 根挂载点特殊处理:直接挂载(因为还没有文件系统放 ready 文件) */
        if (strcmp(cfg->mount, "/") == 0) {
            /* 等待一小段时间让服务初始化 */
            msleep(50);
            do_mount(cfg);
            rt->ready = true;
        } else {
            /* 非根挂载:等待 ready 文件 */
            printf("[svc] Waiting for %s to be ready...\n", cfg->name);
            if (wait_for_ready(cfg->name, 5000)) {
                rt->ready = true;
                do_mount(cfg);
            } else {
                printf("[svc] Timeout waiting for %s\n", cfg->name);
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
