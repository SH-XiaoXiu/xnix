#include <xnix/perm.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/**
 * 检查进程是否拥有权限(热路径)
 *
 * 这是最频繁调用的函数,必须极快(目标 <10ns).
 *
 * @param proc     要检查的进程
 * @param perm_id  权限 ID
 * @return 允许返回 true,否则返回 false
 */
bool perm_check(struct process *proc, perm_id_t perm_id) {
    if (!proc || !proc->perms) {
        return false;
    }

    struct perm_state *ps = proc->perms;

    /* 如果 dirty 则惰性解析 */
    if (__builtin_expect(ps->dirty, 0)) {
        perm_resolve(ps);
    }
    if (__builtin_expect(ps->registry_count_snapshot != perm_registry_count(), 0)) {
        ps->dirty = true;
        perm_resolve(ps);
    }

    if (perm_id == PERM_ID_INVALID) {
        return false;
    }

    /* 边界检查:新权限节点可能在解析后注册,需要重新解析 */
    if (perm_id / 32 >= ps->bitmap_words) {
        ps->dirty = true;
        perm_resolve(ps);
        /* 解析后再次检查边界 */
        if (perm_id / 32 >= ps->bitmap_words) {
            return false;
        }
    }

    /* 快速位图检查 */
    uint32_t word = ps->grant_bitmap[perm_id / 32];
    uint32_t bit  = 1u << (perm_id % 32);
    if ((word & bit) == 0) {
        pr_debug("[PERM] denied: proc=%d perm=%d\n", proc->pid, perm_id);
        return false;
    }
    return true;
}

/**
 * 检查 I/O 端口权限(专用热路径)
 *
 * @param proc  要检查的进程
 * @param port  I/O 端口号 (0-65535)
 * @return 允许返回 true,否则返回 false
 */
bool perm_check_ioport(struct process *proc, uint16_t port) {
    if (!proc || !proc->perms) {
        return false;
    }

    struct perm_state *ps = proc->perms;

    if (__builtin_expect(ps->dirty, 0)) {
        perm_resolve(ps);
    }
    if (__builtin_expect(ps->registry_count_snapshot != perm_registry_count(), 0)) {
        ps->dirty = true;
        perm_resolve(ps);
    }

    if (!ps->ioport_bitmap) {
        return false; /* 无 I/O 端口权限 */
    }

    /* 快速位图检查 */
    uint8_t byte = ps->ioport_bitmap[port / 8];
    uint8_t bit  = 1u << (port % 8);
    return (byte & bit) != 0;
}

/**
 * 按名称检查权限(慢路径,方便使用)
 *
 * 仅在非关键路径使用.热路径应预先解析为 perm_id.
 */
bool perm_check_name(struct process *proc, const char *node) {
    perm_id_t id = perm_lookup(node);
    if (id == PERM_ID_INVALID) {
        return false;
    }
    return perm_check(proc, id);
}

/**
 * 检查 profile 的所有 GRANT 权限是否都在 parent_state 中也是 GRANT
 *
 * 用于强制"子进程权限 ⊆ 父进程权限"的降级约束.
 * 遍历 profile(含继承链)中的所有 GRANT 规则,
 * 逐一检查 parent_state 中是否也授予了相应权限.
 *
 * 特例:如果 parent 拥有 xnix.*(全通配)则任何 profile 都是其子集.
 */
bool perm_profile_is_subset(struct perm_profile *profile, struct perm_state *parent_state) {
    if (!profile || !parent_state) {
        return true;
    }

    /* 确保 parent 的位图是最新的 */
    if (parent_state->dirty) {
        perm_resolve(parent_state);
    }

    /* 检查 parent 是否有 xnix.* (全通配) */
    perm_id_t wildcard_id = perm_lookup("xnix.*");
    if (wildcard_id != PERM_ID_INVALID && perm_check_bitmap(parent_state, wildcard_id)) {
        return true;
    }

    /* 遍历 profile 继承链 */
    struct perm_profile *p = profile;
    while (p) {
        for (uint32_t i = 0; i < p->perm_count; i++) {
            if (p->perms[i].value != PERM_GRANT) {
                continue;
            }
            /* profile 中有 GRANT, 检查 parent 是否也有 */
            perm_id_t id = perm_lookup(p->perms[i].node);
            if (id == PERM_ID_INVALID) {
                /* 节点尚未注册,保守处理:允许 */
                continue;
            }
            if (!perm_check_bitmap(parent_state, id)) {
                pr_debug("[PERM] subset check failed: %s (id=%u)\n", p->perms[i].node, id);
                return false;
            }
        }
        p = p->parent;
    }

    return true;
}
