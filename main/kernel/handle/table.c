#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/string.h>

/**
 * 创建进程 Handle 表
 */
struct handle_table *handle_table_create(void) {
    struct handle_table *table = kmalloc(sizeof(struct handle_table));
    if (!table) {
        return NULL;
    }

    table->capacity = CFG_HANDLE_TABLE_SIZE;
    table->entries  = kmalloc(table->capacity * sizeof(struct handle_entry));
    if (!table->entries) {
        kfree(table);
        return NULL;
    }

    memset(table->entries, 0, table->capacity * sizeof(struct handle_entry));
    spin_init(&table->lock);

    return table;
}

/**
 * 销毁 Handle 表
 */
void handle_table_destroy(struct handle_table *table) {
    if (!table) {
        return;
    }

    spin_lock(&table->lock);

    /* 释放所有 handle */
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].type != HANDLE_NONE) {
            handle_object_put(table->entries[i].type, table->entries[i].object);
            memset(&table->entries[i], 0, sizeof(struct handle_entry));
        }
    }

    kfree(table->entries);
    spin_unlock(&table->lock);
    kfree(table);
}

/**
 * 获取 Handle 表项(内部使用)
 */
struct handle_entry *handle_get_entry(struct handle_table *table, handle_t h) {
    if (!table || h >= table->capacity) {
        return NULL;
    }

    struct handle_entry *entry = &table->entries[h];
    if (entry->type == HANDLE_NONE) {
        return NULL; /* 未分配 */
    }

    return entry;
}
