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

struct ini_ctx {
    struct svc_manager    *mgr;
    struct svc_config     *current;
    struct svc_handle_def *current_handle;
    struct svc_profile    *current_profile;
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
        } else if (strcmp(key, "builtin") == 0) {
            cfg->builtin = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
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
        } else if (strcmp(key, "profile") == 0) {
            size_t len = strlen(value);
            if (len >= sizeof(cfg->profile)) {
                len = sizeof(cfg->profile) - 1;
            }
            memcpy(cfg->profile, value, len);
            cfg->profile[len] = '\0';
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
        } else if (key[0] == 'x' && strncmp(key, "xnix.", 5) == 0) {
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
            } else if (strcmp(value, "inherit") == 0) {
                h->type = SVC_HANDLE_TYPE_INHERIT;
            }
        }
        return true;
    }

    char profile_name[32];
    if (parse_profile_section(section, profile_name, sizeof(profile_name))) {
        ictx->current        = NULL;
        ictx->current_handle = NULL;

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

        if (strcmp(key, "inherit") == 0) {
            strncpy(prof->inherit, value, sizeof(prof->inherit) - 1);
        } else if (key[0] == 'x' && strncmp(key, "xnix.", 5) == 0) {
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
        .mgr             = mgr,
        .current         = NULL,
        .current_handle  = NULL,
        .current_profile = NULL,
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
