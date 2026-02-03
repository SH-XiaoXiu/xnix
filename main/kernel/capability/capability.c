/**
 * @file capability.c
 * @brief 能力系统公共实现
 *
 * 包含静态和动态版本共用的函数.
 * cap_table_create, cap_table_destroy, cap_alloc 由静态或动态版本提供.
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <xnix/config.h>
#include <xnix/mm.h>
#include <xnix/string.h>

/* 对象类型的引用计数函数表 */
static struct {
    cap_ref_fn   ref;
    cap_unref_fn unref;
} cap_type_ops[16];

void cap_register_type(cap_type_t type, cap_ref_fn ref, cap_unref_fn unref) {
    if (type >= 16) {
        return;
    }
    cap_type_ops[type].ref   = ref;
    cap_type_ops[type].unref = unref;
}

void cap_ref_object(cap_type_t type, void *object) {
    if (type < 16 && cap_type_ops[type].ref) {
        cap_type_ops[type].ref(object);
    }
}

void cap_unref_object(cap_type_t type, void *object) {
    if (type < 16 && cap_type_ops[type].unref) {
        cap_type_ops[type].unref(object);
    }
}

/*
 * 公共函数(两个版本共用)
 */

void cap_free(struct process *proc, cap_handle_t handle) {
    if (!proc || !proc->cap_table || handle >= cap_table_capacity(proc->cap_table)) {
        return;
    }

    struct cap_table *table = proc->cap_table;
    uint32_t          flags = cpu_irq_save();
    spin_lock(&table->lock);

    if (table->caps[handle].type == CAP_TYPE_NONE) {
        spin_unlock(&table->lock);
        cpu_irq_restore(flags);
        return;
    }

    /* 减少对象引用计数 */
    cap_unref_object(table->caps[handle].type, table->caps[handle].object);

    /* 释放槽位 */
    table->caps[handle].type   = CAP_TYPE_NONE;
    table->caps[handle].rights = 0;
    table->caps[handle].object = NULL;

    spin_unlock(&table->lock);
    cpu_irq_restore(flags);
}

void *cap_lookup(struct process *proc, cap_handle_t handle, cap_type_t expected_type,
                 cap_rights_t required_rights) {
    if (!proc || !proc->cap_table || handle >= cap_table_capacity(proc->cap_table)) {
        return NULL;
    }

    struct cap_table *table = proc->cap_table;
    uint32_t          flags = cpu_irq_save();
    spin_lock(&table->lock);

    /* 检查类型 */
    if (table->caps[handle].type != expected_type) {
        spin_unlock(&table->lock);
        cpu_irq_restore(flags);
        return NULL;
    }

    /* 检查权限 */
    if ((table->caps[handle].rights & required_rights) != required_rights) {
        spin_unlock(&table->lock);
        cpu_irq_restore(flags);
        return NULL;
    }

    void *object = table->caps[handle].object;

    spin_unlock(&table->lock);
    cpu_irq_restore(flags);
    return object;
}

cap_handle_t cap_duplicate_to(struct process *src, cap_handle_t src_handle, struct process *dst,
                              cap_rights_t new_rights, cap_handle_t hint_dst) {
    if (!src || !src->cap_table || !dst || !dst->cap_table ||
        src_handle >= cap_table_capacity(src->cap_table)) {
        return CAP_HANDLE_INVALID;
    }

    struct cap_table *src_table = src->cap_table;
    uint32_t          flags     = cpu_irq_save();
    spin_lock(&src_table->lock);

    /* 检查源能力 */
    if (src_table->caps[src_handle].type == CAP_TYPE_NONE) {
        spin_unlock(&src_table->lock);
        cpu_irq_restore(flags);
        return CAP_HANDLE_INVALID;
    }

    /* 检查是否有 GRANT 权限 */
    if (!(src_table->caps[src_handle].rights & CAP_GRANT)) {
        spin_unlock(&src_table->lock);
        cpu_irq_restore(flags);
        return CAP_HANDLE_INVALID;
    }

    /* 新权限必须 <= 原权限 */
    if ((new_rights & ~src_table->caps[src_handle].rights) != 0) {
        spin_unlock(&src_table->lock);
        cpu_irq_restore(flags);
        return CAP_HANDLE_INVALID;
    }

    cap_type_t type   = src_table->caps[src_handle].type;
    void      *object = src_table->caps[src_handle].object;

    spin_unlock(&src_table->lock);
    cpu_irq_restore(flags);

    /* 在目标进程分配新句柄(优先使用 hint_dst) */
    return cap_alloc_at(dst, type, object, new_rights, hint_dst);
}

/* 公共 API 实现(基于当前进程) */

int cap_close(cap_handle_t handle) {
    struct process *proc = process_get_current();
    if (!proc) {
        return -1;
    }

    cap_free(proc, handle);
    return 0;
}

cap_handle_t cap_duplicate(cap_handle_t handle, cap_rights_t new_rights) {
    struct process *proc = process_get_current();
    if (!proc) {
        return CAP_HANDLE_INVALID;
    }

    return cap_duplicate_to(proc, handle, proc, new_rights, CAP_HANDLE_INVALID);
}
