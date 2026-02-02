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

static bool parse_cap_section(const char *section, char *name, size_t name_size) {
    const char *prefix     = "cap.";
    size_t      prefix_len = strlen(prefix);

    if (strncmp(section, prefix, prefix_len) != 0) {
        return false;
    }

    const char *cap_name = section + prefix_len;
    size_t      len      = strlen(cap_name);
    if (len == 0 || len >= name_size) {
        return false;
    }

    memcpy(name, cap_name, len);
    name[len] = '\0';
    return true;
}

static struct svc_cap_def *cap_def_find(struct svc_manager *mgr, const char *name) {
    for (int i = 0; i < mgr->cap_def_count; i++) {
        if (strcmp(mgr->cap_defs[i].name, name) == 0) {
            return &mgr->cap_defs[i];
        }
    }
    return NULL;
}

static struct svc_cap_def *cap_def_get_or_add(struct svc_manager *mgr, const char *name) {
    struct svc_cap_def *def = cap_def_find(mgr, name);
    if (def) {
        return def;
    }

    if (mgr->cap_def_count >= SVC_MAX_CAP_DEFS) {
        return NULL;
    }

    def = &mgr->cap_defs[mgr->cap_def_count++];
    memset(def, 0, sizeof(*def));
    snprintf(def->name, sizeof(def->name), "%s", name);
    def->type   = SVC_CAP_TYPE_NONE;
    def->rights = CAP_READ | CAP_WRITE | CAP_GRANT;
    def->handle = CAP_HANDLE_INVALID;
    return def;
}

static int cap_def_create(struct svc_cap_def *def) {
    if (def->created) {
        return 0;
    }

    int h = -1;
    switch (def->type) {
        case SVC_CAP_TYPE_ENDPOINT:
            h = sys_endpoint_create();
            break;
        case SVC_CAP_TYPE_IOPORT:
            h = sys_ioport_create_range(def->ioport_start, def->ioport_end, def->rights);
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

static int cap_get_or_create(struct svc_manager *mgr, const char *name, uint32_t *out_handle,
                             uint32_t *out_rights) {
    struct svc_cap_def *def = cap_def_find(mgr, name);
    if (!def) {
        return -1;
    }
    if (cap_def_create(def) < 0) {
        return -1;
    }
    *out_handle = def->handle;
    *out_rights = def->rights;
    return 0;
}

uint32_t svc_get_ticks(void) {
    return g_ticks;
}

void svc_manager_init(struct svc_manager *mgr) {
    memset(mgr, 0, sizeof(*mgr));

    /* 确保 /run 目录存在 */
    sys_mkdir(SVC_READY_DIR);

    struct svc_cap_def *cap = NULL;

    cap = cap_def_get_or_add(mgr, "serial_ep");
    if (cap) {
        cap->type = SVC_CAP_TYPE_ENDPOINT;
    }

    cap = cap_def_get_or_add(mgr, "ioport");
    if (cap) {
        cap->type         = SVC_CAP_TYPE_IOPORT;
        cap->ioport_start = 0x3F8;
        cap->ioport_end   = 0x3FF;
    }

    cap = cap_def_get_or_add(mgr, "vfs_ep");
    if (cap) {
        cap->type = SVC_CAP_TYPE_ENDPOINT;
    }

    cap = cap_def_get_or_add(mgr, "ata_io");
    if (cap) {
        cap->type         = SVC_CAP_TYPE_IOPORT;
        cap->ioport_start = 0x1F0;
        cap->ioport_end   = 0x1F7;
    }

    cap = cap_def_get_or_add(mgr, "ata_ctrl");
    if (cap) {
        cap->type         = SVC_CAP_TYPE_IOPORT;
        cap->ioport_start = 0x3F6;
        cap->ioport_end   = 0x3F7;
    }

    cap = cap_def_get_or_add(mgr, "fat_vfs_ep");
    if (cap) {
        cap->type = SVC_CAP_TYPE_ENDPOINT;
    }

    cap = cap_def_get_or_add(mgr, "fb_ep");
    if (cap) {
        cap->type = SVC_CAP_TYPE_ENDPOINT;
    }

    cap = cap_def_get_or_add(mgr, "rootfs_ep");
    if (cap) {
        cap->type = SVC_CAP_TYPE_ENDPOINT;
    }
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

static uint32_t parse_u32_auto(const char *s) {
    if (!s) {
        return 0;
    }

    while (*s == ' ' || *s == '\t') {
        s++;
    }

    uint32_t base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }

    uint32_t v = 0;
    while (*s) {
        uint32_t digit = 0;
        char     c     = *s;
        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = 10u + (uint32_t)(c - 'a');
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = 10u + (uint32_t)(c - 'A');
        } else {
            break;
        }

        uint32_t nv = v * base + digit;
        if (nv < v) {
            return 0xFFFFFFFFu;
        }
        v = nv;
        s++;
    }

    return v;
}

static uint16_t parse_u16_auto(const char *s) {
    uint32_t v = parse_u32_auto(s);
    if (v > 0xFFFFu) {
        v = 0xFFFFu;
    }
    return (uint16_t)v;
}

/**
 * 解析 caps 字符串
 * 格式: "cap_name:dst_hint cap_name:dst_hint ..."
 */
int svc_parse_caps(struct svc_manager *mgr, const char *caps_str, struct svc_cap_desc *caps,
                   int max_caps) {
    (void)mgr;
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
            snprintf(caps[count].name, sizeof(caps[count].name), "%s", cap_name);
            caps[count].src_handle = CAP_HANDLE_INVALID;
            caps[count].rights     = 0;
            caps[count].dst_hint   = 0;

            for (const char *s = dst_str; *s; s++) {
                if (*s >= '0' && *s <= '9') {
                    caps[count].dst_hint = caps[count].dst_hint * 10 + (*s - '0');
                }
            }

            count++;
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
    struct svc_cap_def *current_cap;
};

static void svc_resolve_caps(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config *cfg = &mgr->configs[i];

        for (int j = 0; j < cfg->cap_count; j++) {
            struct svc_cap_desc *cap = &cfg->caps[j];
            if (cap->name[0] == '\0') {
                continue;
            }

            if (cap->src_handle == CAP_HANDLE_INVALID) {
                uint32_t h      = CAP_HANDLE_INVALID;
                uint32_t rights = 0;
                if (cap_get_or_create(mgr, cap->name, &h, &rights) < 0) {
                    printf("Unknown capability: %s\n", cap->name);
                    continue;
                }
                cap->src_handle = h;
                cap->rights     = rights;
            }
        }

        if (cfg->mount[0] != '\0' && cfg->cap_count > 0) {
            cfg->mount_ep = cfg->caps[0].src_handle;
        }
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
        ictx->current_cap = NULL;

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
            cfg->cap_count = svc_parse_caps(mgr, value, cfg->caps, SVC_CAPS_MAX);
        } else if (strcmp(key, "mount") == 0) {
            size_t len = strlen(value);
            if (len >= SVC_PATH_MAX) {
                len = SVC_PATH_MAX - 1;
            }
            memcpy(cfg->mount, value, len);
            cfg->mount[len] = '\0';
        }

        return true;
    }

    char cap_name[SVC_CAP_NAME_MAX];
    if (parse_cap_section(section, cap_name, sizeof(cap_name))) {
        ictx->current = NULL;

        if (ictx->current_cap == NULL || strcmp(ictx->current_cap->name, cap_name) != 0) {
            ictx->current_cap = cap_def_get_or_add(mgr, cap_name);
        }

        struct svc_cap_def *cap = ictx->current_cap;
        if (!cap) {
            printf("Too many cap defs\n");
            return true;
        }

        if (strcmp(key, "type") == 0) {
            if (strcmp(value, "endpoint") == 0) {
                cap->type = SVC_CAP_TYPE_ENDPOINT;
            } else if (strcmp(value, "ioport") == 0) {
                cap->type = SVC_CAP_TYPE_IOPORT;
            }
        } else if (strcmp(key, "start") == 0) {
            cap->ioport_start = parse_u16_auto(value);
        } else if (strcmp(key, "end") == 0) {
            cap->ioport_end = parse_u16_auto(value);
        } else if (strcmp(key, "rights") == 0) {
            cap->rights = parse_u32_auto(value) & (CAP_READ | CAP_WRITE | CAP_GRANT);
        }
        return true;
    }

    ictx->current     = NULL;
    ictx->current_cap = NULL;

    return true;
}

int svc_load_config(struct svc_manager *mgr, const char *path) {
    struct ini_ctx ctx = {
        .mgr     = mgr,
        .current = NULL,
        .current_cap = NULL,
    };

    int ret = ini_parse_file(path, ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    svc_resolve_caps(mgr);
    printf("Loaded %d services from %s\n", mgr->count, path);
    return 0;
}

int svc_load_config_string(struct svc_manager *mgr, const char *content) {
    struct ini_ctx ctx = {
        .mgr     = mgr,
        .current = NULL,
        .current_cap = NULL,
    };

    int ret = ini_parse_buffer(content, strlen(content), ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    svc_resolve_caps(mgr);
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

    printf("Mounting %s on %s (ep=%u)\n", cfg->name, cfg->mount, cfg->mount_ep);
    int ret = sys_mount(cfg->mount, cfg->mount_ep);
    if (ret < 0) {
        printf("Failed to mount %s: %d\n", cfg->mount, ret);
    }
    return ret;
}

int svc_start_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    printf("Starting %s...\n", cfg->name);

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
        printf("Failed to start %s: %d\n", cfg->name, pid);
        rt->state = SVC_STATE_FAILED;
        return pid;
    }

    printf("%s started (pid=%d)\n", cfg->name, pid);
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
            printf("Waiting for %s to be ready...\n", cfg->name);
            if (wait_for_ready(cfg->name, 5000)) {
                rt->ready = true;
                do_mount(cfg);
            } else {
                printf("Timeout waiting for %s\n", cfg->name);
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
            sys_del(ready_path);

            /* 检查是否需要重启 */
            if (cfg->respawn) {
                printf("Respawning %s...\n", cfg->name);
                rt->state = SVC_STATE_PENDING;
            }

            return;
        }
    }
}
