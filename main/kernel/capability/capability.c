/**
 * @file capability.c
 * @brief 能力系统实现
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <xnix/config.h>
#include <xnix/mm.h>

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

static void cap_ref_object(cap_type_t type, void *object) {
    if (type < 16 && cap_type_ops[type].ref) {
        cap_type_ops[type].ref(object);
    }
}

static void cap_unref_object(cap_type_t type, void *object) {
    if (type < 16 && cap_type_ops[type].unref) {
        cap_type_ops[type].unref(object);
    }
}

struct cap_table *cap_table_create(void) {
    struct cap_table *table = kzalloc(sizeof(struct cap_table));
    if (!table) {
        return NULL;
    }

    /* 初始化所有槽位为空 */
    for (uint32_t i = 0; i < CFG_CAP_TABLE_SIZE; i++) {
        table->caps[i].type   = CAP_TYPE_NONE;
        table->caps[i].rights = 0;
        table->caps[i].object = NULL;
    }

    return table;
}

void cap_table_destroy(struct cap_table *table) {
    if (!table) {
        return;
    }

    /* 释放所有能力 */
    for (uint32_t i = 0; i < CFG_CAP_TABLE_SIZE; i++) {
        if (table->caps[i].type != CAP_TYPE_NONE) {
            cap_unref_object(table->caps[i].type, table->caps[i].object);
        }
    }

    kfree(table);
}

cap_handle_t cap_alloc(struct process *proc, cap_type_t type, void *object, cap_rights_t rights) {
    if (!proc || !proc->cap_table || !object) {
        return CAP_HANDLE_INVALID;
    }

    struct cap_table *table = proc->cap_table;
    uint32_t          flags = cpu_irq_save();
    spin_lock(&table->lock);

    /* 查找空闲槽位 */
    cap_handle_t handle = CAP_HANDLE_INVALID;
    for (uint32_t i = 0; i < CFG_CAP_TABLE_SIZE; i++) {
        if (table->caps[i].type == CAP_TYPE_NONE) {
            handle = i;
            break;
        }
    }

    if (handle == CAP_HANDLE_INVALID) {
        spin_unlock(&table->lock);
        cpu_irq_restore(flags);
        return CAP_HANDLE_INVALID;
    }

    /* 分配能力 */
    table->caps[handle].type   = type;
    table->caps[handle].rights = rights;
    table->caps[handle].object = object;

    /* 增加对象引用计数 */
    cap_ref_object(type, object);

    spin_unlock(&table->lock);
    cpu_irq_restore(flags);
    return handle;
}

void cap_free(struct process *proc, cap_handle_t handle) {
    if (!proc || !proc->cap_table || handle >= CFG_CAP_TABLE_SIZE) {
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
    if (!proc || !proc->cap_table || handle >= CFG_CAP_TABLE_SIZE) {
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
                              cap_rights_t new_rights) {
    if (!src || !src->cap_table || !dst || !dst->cap_table || src_handle >= CFG_CAP_TABLE_SIZE) {
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

    /* 在目标进程分配新句柄 */
    return cap_alloc(dst, type, object, new_rights);
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

    return cap_duplicate_to(proc, handle, proc, new_rights);
}
