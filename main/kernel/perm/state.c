#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/**
 * 创建进程权限状态
 *
 * @param profile  可选的初始 Profile
 * @return 初始化后的权限状态,失败返回 NULL
 */
struct perm_state *perm_state_create(struct perm_profile *profile) {
    struct perm_state *state = kmalloc(sizeof(struct perm_state));
    if (!state) {
        return NULL;
    }

    state->profile        = profile;
    state->overrides      = NULL;
    state->override_count = 0;

    /* 分配位图(1 bit 对应一个权限 ID) */
    uint32_t max_perms = perm_registry_count();
    /* 确保至少分配一个字,避免零大小分配 */
    if (max_perms == 0) {
        max_perms = 32;
    }

    state->bitmap_words = (max_perms + 31) / 32;
    state->grant_bitmap = kmalloc(state->bitmap_words * sizeof(uint32_t));
    if (!state->grant_bitmap) {
        kfree(state);
        return NULL;
    }
    memset(state->grant_bitmap, 0, state->bitmap_words * sizeof(uint32_t));
    state->registry_count_snapshot = max_perms;

    /* I/O 端口位图(65536 bits = 8KB)按需分配 */
    state->ioport_bitmap = NULL;

    state->dirty = true; /* 需要解析 */
    spin_init(&state->lock);

    return state;
}

/**
 * 销毁权限状态
 */
void perm_state_destroy(struct perm_state *state) {
    if (!state) {
        return;
    }

    if (state->grant_bitmap) {
        kfree(state->grant_bitmap);
    }
    if (state->ioport_bitmap) {
        kfree(state->ioport_bitmap);
    }
    if (state->overrides) {
        /* 释放 kstrdup 的字符串 */
        for (uint32_t i = 0; i < state->override_count; i++) {
            kfree((void *)state->overrides[i].node);
        }
        kfree(state->overrides);
    }
    kfree(state);
}

/**
 * 授予进程权限
 *
 * @param state  权限状态
 * @param node   权限节点名称
 * @return 成功返回 0,失败返回负数错误码
 */
int perm_grant(struct perm_state *state, const char *node) {
    if (!state || !node) {
        return -1; // EINVAL
    }

    spin_lock(&state->lock);

    /* 添加到覆盖列表 */
    void *new_overrides =
        krealloc(state->overrides, (state->override_count + 1) * sizeof(struct perm_entry));
    if (!new_overrides) {
        spin_unlock(&state->lock);
        return -1; // ENOMEM
    }
    state->overrides = new_overrides;

    state->overrides[state->override_count].node  = kstrdup(node);
    state->overrides[state->override_count].value = PERM_GRANT;
    state->override_count++;

    /* 标记 dirty 以触发重新解析 */
    state->dirty = true;

    spin_unlock(&state->lock);
    return 0;
}

/**
 * 拒绝进程权限
 */
int perm_deny(struct perm_state *state, const char *node) {
    if (!state || !node) {
        return -1; // EINVAL
    }

    spin_lock(&state->lock);

    /* 添加到覆盖列表 */
    void *new_overrides =
        krealloc(state->overrides, (state->override_count + 1) * sizeof(struct perm_entry));
    if (!new_overrides) {
        spin_unlock(&state->lock);
        return -1; // ENOMEM
    }
    state->overrides = new_overrides;

    state->overrides[state->override_count].node  = kstrdup(node);
    state->overrides[state->override_count].value = PERM_DENY;
    state->override_count++;

    /* 标记 dirty 以触发重新解析 */
    state->dirty = true;

    spin_unlock(&state->lock);
    return 0;
}

/**
 * 关联 Profile 到进程
 */
void perm_state_attach_profile(struct perm_state *state, struct perm_profile *profile) {
    if (!state) {
        return;
    }

    spin_lock(&state->lock);
    state->profile = profile;
    state->dirty   = true; /* 需要重新解析 */
    spin_unlock(&state->lock);
}

/**
 * 辅助函数:直接检查位图(内部使用)
 */
bool perm_check_bitmap(struct perm_state *state, perm_id_t id) {
    if (!state || !state->grant_bitmap) {
        return false;
    }
    if (id / 32 >= state->bitmap_words) {
        return false;
    }

    return (state->grant_bitmap[id / 32] >> (id % 32)) & 1;
}
