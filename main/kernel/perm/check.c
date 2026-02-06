#include <xnix/perm.h>
#include <xnix/process_def.h>

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
    return (word & bit) != 0;
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
