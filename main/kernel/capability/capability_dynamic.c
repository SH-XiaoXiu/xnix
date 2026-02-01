/**
 * @file capability_dynamic.c
 * @brief 能力表动态实现(强符号)
 *
 * 可扩展数组,容量不足时自动扩容.
 * 编译此文件时会覆盖 capability_static.c 中的弱符号.
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <xnix/config.h>
#include <xnix/mm.h>
#include <xnix/string.h>

#define CAP_TABLE_MAX_SIZE 4096

/**
 * 扩展能力表(调用时必须持有锁)
 */
static bool cap_table_expand(struct cap_table *table) {
    uint32_t new_cap = table->capacity * 2;
    if (new_cap > CAP_TABLE_MAX_SIZE) {
        new_cap = CAP_TABLE_MAX_SIZE;
    }
    if (new_cap <= table->capacity) {
        return false;
    }

    struct capability *new_caps = kzalloc(new_cap * sizeof(struct capability));
    if (!new_caps) {
        return false;
    }

    memcpy(new_caps, table->caps, table->capacity * sizeof(struct capability));

    for (uint32_t i = table->capacity; i < new_cap; i++) {
        new_caps[i].type   = CAP_TYPE_NONE;
        new_caps[i].rights = 0;
        new_caps[i].object = NULL;
    }

    kfree(table->caps);
    table->caps     = new_caps;
    table->capacity = new_cap;

    return true;
}

struct cap_table *cap_table_create(void) {
    struct cap_table *table = kzalloc(sizeof(struct cap_table));
    if (!table) {
        return NULL;
    }

    table->capacity = CFG_CAP_TABLE_SIZE;
    table->caps     = kzalloc(table->capacity * sizeof(struct capability));
    if (!table->caps) {
        kfree(table);
        return NULL;
    }

    for (uint32_t i = 0; i < table->capacity; i++) {
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

    extern void cap_unref_object(cap_type_t type, void *object);
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->caps[i].type != CAP_TYPE_NONE) {
            cap_unref_object(table->caps[i].type, table->caps[i].object);
        }
    }

    kfree(table->caps);
    kfree(table);
}

uint32_t cap_table_capacity(struct cap_table *table) {
    return table ? table->capacity : 0;
}

cap_handle_t cap_alloc(struct process *proc, cap_type_t type, void *object, cap_rights_t rights) {
    if (!proc || !proc->cap_table || !object) {
        return CAP_HANDLE_INVALID;
    }

    extern void cap_ref_object(cap_type_t type, void *object);

    struct cap_table *table = proc->cap_table;
    uint32_t          flags = cpu_irq_save();
    spin_lock(&table->lock);

    cap_handle_t handle = CAP_HANDLE_INVALID;
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->caps[i].type == CAP_TYPE_NONE) {
            handle = i;
            break;
        }
    }

    /* 扩容 */
    if (handle == CAP_HANDLE_INVALID) {
        if (cap_table_expand(table)) {
            for (uint32_t i = 0; i < table->capacity; i++) {
                if (table->caps[i].type == CAP_TYPE_NONE) {
                    handle = i;
                    break;
                }
            }
        }
    }

    if (handle == CAP_HANDLE_INVALID) {
        spin_unlock(&table->lock);
        cpu_irq_restore(flags);
        return CAP_HANDLE_INVALID;
    }

    table->caps[handle].type   = type;
    table->caps[handle].rights = rights;
    table->caps[handle].object = object;
    cap_ref_object(type, object);

    spin_unlock(&table->lock);
    cpu_irq_restore(flags);
    return handle;
}
