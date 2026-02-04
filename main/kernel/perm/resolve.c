#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

#define PERM_NODE_NAME_MAX 64

/* 简单的 strtoul 实现 */
static unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base) {
    unsigned long result = 0;
    if (!base) {
        base = 10;
    }
    while (*cp) {
        unsigned int value;
        if (*cp >= '0' && *cp <= '9') {
            value = *cp - '0';
        } else if (*cp >= 'a' && *cp <= 'f') {
            value = *cp - 'a' + 10;
        } else if (*cp >= 'A' && *cp <= 'F') {
            value = *cp - 'A' + 10;
        } else {
            break;
        }
        if (value >= base) {
            break;
        }
        result = result * base + value;
        cp++;
    }
    if (endp) {
        *endp = (char *)cp;
    }
    return result;
}

#define strtoul simple_strtoul

/* 前向声明 */
static void resolve_profile_recursive(struct perm_state *state, struct perm_profile *profile);
static void expand_wildcard(struct perm_state *state, const char *wildcard, perm_value_t value);
static void set_bitmap(struct perm_state *state, perm_id_t id, perm_value_t value);
static void resolve_ioport_bitmap(struct perm_state *state);

/**
 * 解析进程权限(冷路径)
 *
 * 此操作开销较大(通配符展开,继承遍历),但仅在权限状态变更时调用.
 *
 * @param state  需要解析的权限状态
 */
void perm_resolve(struct perm_state *state) {
    if (!state) {
        return;
    }

    spin_lock(&state->lock);

    if (!state->dirty) {
        spin_unlock(&state->lock);
        return; /* 已解析 */
    }

    /* 如果注册表增长,调整位图大小 */
    uint32_t current_count = perm_registry_count();
    uint32_t needed_words  = (current_count + 31) / 32;
    if (needed_words > state->bitmap_words) {
        /* 手动实现 realloc 逻辑 */
        size_t old_size   = state->bitmap_words * sizeof(uint32_t);
        size_t new_size   = needed_words * sizeof(uint32_t);
        void  *new_bitmap = kzalloc(new_size);
        if (new_bitmap) {
            if (state->grant_bitmap) {
                memcpy(new_bitmap, state->grant_bitmap, old_size);
                kfree(state->grant_bitmap);
            }
            state->grant_bitmap = new_bitmap;
            state->bitmap_words = needed_words;
        }
    }

    /* 清空位图 */
    memset(state->grant_bitmap, 0, state->bitmap_words * sizeof(uint32_t));

    /* 步骤 1:解析 Profile 权限(包含继承) */
    if (state->profile) {
        resolve_profile_recursive(state, state->profile);
    }

    /* 步骤 2:应用进程级覆盖 */
    for (uint32_t i = 0; i < state->override_count; i++) {
        const char  *node  = state->overrides[i].node;
        perm_value_t value = state->overrides[i].value;

        if (strchr(node, '*')) {
            /* 通配符:展开并应用 */
            expand_wildcard(state, node, value);
        } else {
            /* 具体节点:直接设置 */
            perm_id_t id = perm_lookup(node);
            if (id != PERM_ID_INVALID) {
                set_bitmap(state, id, value);
            }
        }
    }

    /* 步骤 3:构建 I/O 端口位图 */
    resolve_ioport_bitmap(state);

    state->registry_count_snapshot = current_count;
    state->dirty                   = false;
    spin_unlock(&state->lock);
}

/**
 * 递归解析 Profile 权限
 */
static void resolve_profile_recursive(struct perm_state *state, struct perm_profile *profile) {
    if (!profile) {
        return;
    }

    /* 先解析父 Profile(深度优先) */
    if (profile->parent) {
        resolve_profile_recursive(state, profile->parent);
    }

    /* 应用当前 Profile 的权限 */
    for (uint32_t i = 0; i < profile->perm_count; i++) {
        const char  *node  = profile->perms[i].node;
        perm_value_t value = profile->perms[i].value;

        if (strchr(node, '*')) {
            expand_wildcard(state, node, value);
        } else {
            perm_id_t id = perm_lookup(node);
            if (id != PERM_ID_INVALID) {
                set_bitmap(state, id, value);
            }
        }
    }
}

/**
 * 展开通配符权限节点
 *
 * 示例:"xnix.ipc.*" 匹配 "xnix.ipc.send", "xnix.ipc.recv" 等.
 */
static void expand_wildcard(struct perm_state *state, const char *wildcard, perm_value_t value) {
    /* 获取 '*' 前的前缀 */
    char        prefix[PERM_NODE_NAME_MAX];
    const char *star = strchr(wildcard, '*');
    if (!star) {
        return;
    }

    size_t prefix_len = star - wildcard;
    if (prefix_len >= PERM_NODE_NAME_MAX) {
        prefix_len = PERM_NODE_NAME_MAX - 1;
    }

    strncpy(prefix, wildcard, prefix_len);
    prefix[prefix_len] = '\0';

    /* 匹配所有以该前缀开头的注册节点 */
    uint32_t count = perm_registry_count();
    for (perm_id_t id = 0; id < count; id++) {
        const char *name = perm_get_name(id);
        if (name && strncmp(name, prefix, prefix_len) == 0) {
            set_bitmap(state, id, value);
        }
    }
}

/**
 * 设置位图中的位
 */
static void set_bitmap(struct perm_state *state, perm_id_t id, perm_value_t value) {
    if (id / 32 >= state->bitmap_words) {
        return;
    }

    if (value == PERM_GRANT) {
        state->grant_bitmap[id / 32] |= (1u << (id % 32));
    } else if (value == PERM_DENY) {
        state->grant_bitmap[id / 32] &= ~(1u << (id % 32));
    }
    /* PERM_UNDEFINED: 不做操作(继承自父级) */
}

/**
 * 从权限节点构建 I/O 端口位图
 *
 * 扫描类似 "xnix.io.port.0x3f8" 或 "xnix.io.port.0x3f8-0x3ff" 的节点.
 */
static void resolve_ioport_bitmap(struct perm_state *state) {
    bool has_ioport_perm = false;

    /* 检查进程是否有任何 I/O 端口权限 */
    uint32_t count = perm_registry_count();
    for (perm_id_t id = 0; id < count; id++) {
        const char *name = perm_get_name(id);
        if (name && strncmp(name, "xnix.io.port.", 13) == 0) {
            /* 检查该具体权限是否在位图中被授予 */
            if (perm_check_bitmap(state, id)) {
                has_ioport_perm = true;
                break;
            }
        }
    }

    if (!has_ioport_perm) {
        /* 无 I/O 端口权限,释放位图 */
        if (state->ioport_bitmap) {
            kfree(state->ioport_bitmap);
            state->ioport_bitmap = NULL;
        }
        return;
    }

    /* 分配 I/O 端口位图(8KB) */
    if (!state->ioport_bitmap) {
        state->ioport_bitmap = kmalloc(8192); /* 65536 bits */
        if (!state->ioport_bitmap) {
            return;
        }
        memset(state->ioport_bitmap, 0, 8192);
    } else {
        /* 清空现有位图以重新构建 */
        memset(state->ioport_bitmap, 0, 8192);
    }

    /* 为每个授予的端口设置位 */
    for (perm_id_t id = 0; id < count; id++) {
        const char *name = perm_get_name(id);
        if (!name || strncmp(name, "xnix.io.port.", 13) != 0) {
            continue;
        }

        if (!perm_check_bitmap(state, id)) {
            continue; /* 未授予 */
        }

        const char *port_spec = name + 13; /* 跳过 "xnix.io.port." */

        if (strchr(port_spec, '-')) {
            /* 范围:"0x3f8-0x3ff" */
            uint32_t start, end;

            /* 解析起始 */
            char *dash = strchr(port_spec, '-');
            if (!dash) {
                continue;
            }

            start = strtoul(port_spec, NULL, 16);
            end   = strtoul(dash + 1, NULL, 16);

            for (uint32_t port = start; port <= end && port < 65536; port++) {
                state->ioport_bitmap[port / 8] |= (1u << (port % 8));
            }
        } else if (strcmp(port_spec, "*") == 0) {
            /* 所有端口 */
            memset(state->ioport_bitmap, 0xFF, 8192);
        } else {
            /* 单个端口:"0x3f8" */
            uint32_t port = strtoul(port_spec, NULL, 16);
            if (port < 65536) {
                state->ioport_bitmap[port / 8] |= (1u << (port % 8));
            }
        }
    }
}
