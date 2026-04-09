#include "svc_internal.h"

#include <stdio.h>
#include <string.h>

#include "../ini_parser.h"

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

static int parse_dep_list(const char *value, char deps[][SVC_NAME_MAX], int max_deps) {
    int         count = 0;
    const char *p     = value;

    while (*p && count < max_deps) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

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

struct ini_ctx {
    struct svc_manager    *mgr;
    struct svc_config     *current;
    struct svc_handle_def *current_handle;
};

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
                cfg->type                   = SVC_TYPE_PATH;
            }
            ictx->current = &mgr->configs[idx];
        }

        struct svc_config *cfg = ictx->current;

        if (strcmp(key, "type") == 0) {
            if (strcmp(value, "path") == 0) {
                cfg->type = SVC_TYPE_PATH;
            }
        } else if (strcmp(key, "path") == 0) {
            const char *actual = value;
            if (strncmp(value, "ramfs://", 8) == 0) {
                cfg->ramfs_load = true;
                actual = value + 8;
            }
            size_t len = strlen(actual);
            if (len >= SVC_PATH_MAX) {
                len = SVC_PATH_MAX - 1;
            }
            memcpy(cfg->path, actual, len);
            cfg->path[len] = '\0';
        } else if (strcmp(key, "args") == 0) {
            size_t len = strlen(value);
            if (len >= sizeof(cfg->args)) {
                len = sizeof(cfg->args) - 1;
            }
            memcpy(cfg->args, value, len);
            cfg->args[len] = '\0';
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
        } else if (strcmp(key, "handles") == 0) {
            cfg->handle_count = svc_parse_handles(mgr, value, cfg->handles, SVC_HANDLES_MAX);
        } else if (strcmp(key, "mount") == 0) {
            size_t len = strlen(value);
            if (len >= SVC_PATH_MAX) {
                len = SVC_PATH_MAX - 1;
            }
            memcpy(cfg->mount, value, len);
            cfg->mount[len] = '\0';
        } else if (strcmp(key, "dirs") == 0) {
            /* 解析空格分隔的目录路径列表 */
            cfg->dirs_count = 0;
            const char *p = value;
            while (*p && cfg->dirs_count < SVC_DEPS_MAX) {
                while (*p == ' ') p++;
                if (!*p) break;
                const char *start = p;
                while (*p && *p != ' ') p++;
                size_t len = (size_t)(p - start);
                if (len >= SVC_PATH_MAX) len = SVC_PATH_MAX - 1;
                memcpy(cfg->dirs[cfg->dirs_count], start, len);
                cfg->dirs[cfg->dirs_count][len] = '\0';
                cfg->dirs_count++;
            }
        } else if (strcmp(key, "stdio") == 0) {
            size_t len = strlen(value);
            if (len >= SVC_HANDLE_NAME_MAX) {
                len = SVC_HANDLE_NAME_MAX - 1;
            }
            memcpy(cfg->stdio, value, len);
            cfg->stdio[len] = '\0';
        } else if (strcmp(key, "provides") == 0) {
            int idx = svc_find_by_name(mgr, cfg->name);
            if (idx >= 0) {
                struct svc_graph_node *node = &mgr->graph[idx];
                node->provides_count        = parse_dep_list(value, node->provides, SVC_DEPS_MAX);
            }
        } else if (strcmp(key, "requires") == 0) {
            int idx = svc_find_by_name(mgr, cfg->name);
            if (idx >= 0) {
                struct svc_graph_node *node = &mgr->graph[idx];
                node->requires_count        = parse_dep_list(value, node->requires, SVC_DEPS_MAX);
            }
        } else if (strcmp(key, "wants") == 0) {
            int idx = svc_find_by_name(mgr, cfg->name);
            if (idx >= 0) {
                struct svc_graph_node *node = &mgr->graph[idx];
                node->wants_count           = parse_dep_list(value, node->wants, SVC_DEPS_MAX);
            }
        }

        return true;
    }

    char handle_name[SVC_HANDLE_NAME_MAX];
    if (parse_handle_section(section, handle_name, sizeof(handle_name))) {
        ictx->current = NULL;

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
            } else if (strcmp(value, "inherit") == 0) {
                h->type = SVC_HANDLE_TYPE_INHERIT;
            }
        }
        return true;
    }

    return true;
}

int svc_load_config(struct svc_manager *mgr, const char *path) {
    struct ini_ctx ctx = {
        .mgr            = mgr,
        .current        = NULL,
        .current_handle = NULL,
    };

    int ret = ini_parse_file(path, ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    if (svc_resolve_service_discovery(mgr) < 0) {
        printf("Failed to resolve service discovery\n");
        return -1;
    }

    svc_resolve_handles(mgr);

    if (svc_build_dependency_graph(mgr) < 0) {
        printf("Failed to build dependency graph\n");
        return -1;
    }

    printf("Loaded %d services from %s\n", mgr->count, path);
    return 0;
}

int svc_load_config_string(struct svc_manager *mgr, const char *content) {
    struct ini_ctx ctx = {
        .mgr            = mgr,
        .current        = NULL,
        .current_handle = NULL,
    };

    int ret = ini_parse_buffer(content, strlen(content), ini_handler, &ctx);
    if (ret < 0) {
        return ret;
    }

    if (svc_resolve_service_discovery(mgr) < 0) {
        printf("Failed to resolve service discovery\n");
        return -1;
    }

    svc_resolve_handles(mgr);

    if (svc_build_dependency_graph(mgr) < 0) {
        printf("Failed to build dependency graph\n");
        return -1;
    }

    printf("Loaded %d services from embedded config\n", mgr->count);
    return 0;
}
