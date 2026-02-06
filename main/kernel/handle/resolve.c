#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/perm.h>
#include <xnix/process_def.h>
#include <xnix/string.h>

int handle_acquire(struct process *proc, handle_t h, handle_type_t expected_type,
                   struct handle_entry *out_entry) {
    if (!proc || !proc->handles || !out_entry) {
        return -EINVAL;
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    if (h >= table->capacity) {
        spin_unlock(&table->lock);
        return -EINVAL;
    }

    struct handle_entry *entry = &table->entries[h];
    if (entry->type == HANDLE_NONE) {
        spin_unlock(&table->lock);
        return -EINVAL;
    }

    if (expected_type != HANDLE_NONE && entry->type != expected_type) {
        spin_unlock(&table->lock);
        return -EINVAL;
    }

    memcpy(out_entry, entry, sizeof(*out_entry));
    handle_object_get(out_entry->type, out_entry->object);

    spin_unlock(&table->lock);
    return 0;
}

/**
 * 解析 Handle 为内核对象(带类型和权限检查)
 *
 * @param proc          拥有 Handle 的进程
 * @param h             Handle ID
 * @param expected_type 期望的 Handle 类型
 * @param required_perm 需要的权限 ID(PERM_ID_INVALID 表示不检查)
 * @return 内核对象指针,失败返回 NULL
 */
void *handle_resolve(struct process *proc, handle_t h, handle_type_t expected_type,
                     perm_id_t required_perm) {
    if (!proc || !proc->handles) {
        return NULL;
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    /* 获取表项 */
    if (h >= table->capacity) {
        spin_unlock(&table->lock);
        return NULL;
    }

    struct handle_entry *entry = &table->entries[h];

    /* 类型检查 */
    if (entry->type != expected_type) {
        spin_unlock(&table->lock);
        return NULL;
    }

    void *object = entry->object;
    spin_unlock(&table->lock);

    /* 权限检查(如果需要) */
    if (required_perm != PERM_ID_INVALID) {
        if (!perm_check(proc, required_perm)) {
            return NULL; /* 权限拒绝 */
        }
    }

    return object;
}

/**
 * 按名称查找 Handle
 *
 * @param proc  在哪个进程中查找
 * @param name  Handle 名称
 * @return Handle ID,未找到返回 HANDLE_INVALID
 */
handle_t handle_find(struct process *proc, const char *name) {
    if (!proc || !proc->handles || !name) {
        return HANDLE_INVALID;
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].type != HANDLE_NONE && strcmp(table->entries[i].name, name) == 0) {
            spin_unlock(&table->lock);
            return i;
        }
    }

    spin_unlock(&table->lock);
    return HANDLE_INVALID;
}
