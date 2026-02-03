/**
 * @file capability_static.c
 * @brief 能力表静态实现(弱符号)
 *
 * 固定大小数组,不支持扩展.
 * 当编译动态版本时,强符号会覆盖这些函数.
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <xnix/config.h>
#include <xnix/mm.h>

__attribute__((weak)) struct cap_table *cap_table_create(void) {
    struct cap_table *table = kzalloc(sizeof(struct cap_table));
    if (!table) {
        return NULL;
    }

    for (uint32_t i = 0; i < CFG_CAP_TABLE_SIZE; i++) {
        table->caps[i].type   = CAP_TYPE_NONE;
        table->caps[i].rights = 0;
        table->caps[i].object = NULL;
    }

    return table;
}

__attribute__((weak)) void cap_table_destroy(struct cap_table *table) {
    if (!table) {
        return;
    }

    extern void cap_unref_object(cap_type_t type, void *object);
    for (uint32_t i = 0; i < CFG_CAP_TABLE_SIZE; i++) {
        if (table->caps[i].type != CAP_TYPE_NONE) {
            cap_unref_object(table->caps[i].type, table->caps[i].object);
        }
    }

    kfree(table);
}

__attribute__((weak)) uint32_t cap_table_capacity(struct cap_table *table) {
    (void)table;
    return CFG_CAP_TABLE_SIZE;
}

__attribute__((weak)) cap_handle_t cap_alloc(struct process *proc, cap_type_t type, void *object,
                                             cap_rights_t rights) {
    return cap_alloc_at(proc, type, object, rights, CAP_HANDLE_INVALID);
}

__attribute__((weak)) cap_handle_t cap_alloc_at(struct process *proc, cap_type_t type, void *object,
                                                cap_rights_t rights, cap_handle_t hint_slot) {
    if (!proc || !proc->cap_table || !object) {
        return CAP_HANDLE_INVALID;
    }

    extern void cap_ref_object(cap_type_t type, void *object);

    struct cap_table *table = proc->cap_table;
    uint32_t          flags = cpu_irq_save();
    spin_lock(&table->lock);

    cap_handle_t handle = CAP_HANDLE_INVALID;

    /* 优先尝试使用 hint_slot */
    if (hint_slot != CAP_HANDLE_INVALID && hint_slot < CFG_CAP_TABLE_SIZE) {
        if (table->caps[hint_slot].type == CAP_TYPE_NONE) {
            handle = hint_slot;
        }
    }

    /* 如果 hint 不可用,查找第一个空闲槽 */
    if (handle == CAP_HANDLE_INVALID) {
        for (uint32_t i = 0; i < CFG_CAP_TABLE_SIZE; i++) {
            if (table->caps[i].type == CAP_TYPE_NONE) {
                handle = i;
                break;
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
