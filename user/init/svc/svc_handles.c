#include "svc_internal.h"

#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>

static struct svc_handle_def *handle_def_find(struct svc_manager *mgr, const char *name) {
    for (int i = 0; i < mgr->handle_def_count; i++) {
        if (strcmp(mgr->handle_defs[i].name, name) == 0) {
            return &mgr->handle_defs[i];
        }
    }
    return NULL;
}

struct svc_handle_def *handle_def_get_or_add(struct svc_manager *mgr, const char *name) {
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
    case SVC_HANDLE_TYPE_INHERIT:
        h = sys_handle_find(def->name);
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

        if (strchr(spec, ':') != NULL) {
            printf("Invalid handle spec '%s' (':' syntax is not supported)\n", spec);
            continue;
        }

        snprintf(handles[count].name, sizeof(handles[count].name), "%s", spec);
        handles[count].src_handle = HANDLE_INVALID;

        struct svc_handle_def *def = handle_def_get_or_add(mgr, spec);
        if (def && def->type == SVC_HANDLE_TYPE_NONE) {
            /* module_*, fb_mem, vga_mem 是内核注入的 handle, 使用 inherit */
            if (strncmp(spec, "module_", 7) == 0 || strcmp(spec, "fb_mem") == 0 ||
                strcmp(spec, "vga_mem") == 0) {
                def->type = SVC_HANDLE_TYPE_INHERIT;
            } else {
                def->type = SVC_HANDLE_TYPE_ENDPOINT;
            }
        }

        count++;
    }

    return count;
}

void svc_resolve_handles(struct svc_manager *mgr) {
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
    }
}

/**
 * 检查服务是否已经持有指定名称的 handle
 */
static bool svc_has_handle(struct svc_config *cfg, const char *name) {
    for (int i = 0; i < cfg->handle_count; i++) {
        if (strcmp(cfg->handles[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

int svc_resolve_service_discovery(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config     *cfg  = &mgr->configs[i];
        struct svc_graph_node *node = &mgr->graph[i];

        for (int j = 0; j < node->provides_count; j++) {
            const char *ep_name = node->provides[j];

            struct svc_handle_def *def = handle_def_get_or_add(mgr, ep_name);
            if (!def) {
                printf("ERROR: Too many handle definitions\n");
                return -1;
            }

            if (def->type == SVC_HANDLE_TYPE_NONE) {
                def->type = SVC_HANDLE_TYPE_ENDPOINT;
            }

            /* 跳过已持有的 handle (幂等) */
            if (svc_has_handle(cfg, ep_name)) {
                continue;
            }

            if (cfg->handle_count >= SVC_HANDLES_MAX) {
                printf("ERROR: Service '%s' has too many handles\n", cfg->name);
                return -1;
            }

            struct svc_handle_desc *h = &cfg->handles[cfg->handle_count++];
            strncpy(h->name, ep_name, SVC_HANDLE_NAME_MAX);
            h->src_handle = HANDLE_INVALID;

            printf("Service '%s' provides '%s'\n", cfg->name, ep_name);
        }
    }

    for (int i = 0; i < mgr->count; i++) {
        struct svc_config     *cfg  = &mgr->configs[i];
        struct svc_graph_node *node = &mgr->graph[i];

        for (int j = 0; j < node->requires_count; j++) {
            const char *ep_name = node->requires[j];

            struct svc_handle_def *def = handle_def_find(mgr, ep_name);
            if (!def) {
                printf("ERROR: Service '%s' requires unknown handle '%s'\n", cfg->name, ep_name);
                return -1;
            }

            if (svc_has_handle(cfg, ep_name)) {
                continue;
            }

            if (cfg->handle_count >= SVC_HANDLES_MAX) {
                printf("ERROR: Service '%s' has too many handles\n", cfg->name);
                return -1;
            }

            struct svc_handle_desc *h = &cfg->handles[cfg->handle_count++];
            strncpy(h->name, ep_name, SVC_HANDLE_NAME_MAX);
            h->src_handle = HANDLE_INVALID;

            printf("Service '%s' requires '%s'\n", cfg->name, ep_name);
        }

        for (int j = 0; j < node->wants_count; j++) {
            const char *ep_name = node->wants[j];

            struct svc_handle_def *def = handle_def_find(mgr, ep_name);
            if (!def) {
                continue;
            }

            if (svc_has_handle(cfg, ep_name)) {
                continue;
            }

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
