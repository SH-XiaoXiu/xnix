#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/string.h>

/* 前向声明:内部函数 */
static void handle_free_entry(struct handle_entry *entry);

/**
 * 创建进程 Handle 表
 */
struct handle_table *handle_table_create(void) {
    struct handle_table *table = kmalloc(sizeof(struct handle_table));
    if (!table) {
        return NULL;
    }

    table->capacity = HANDLE_TABLE_INITIAL_SIZE;
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
            handle_free_entry(&table->entries[i]);
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

/**
 * 释放 Handle 表项资源(内部辅助函数)
 * 需要引用相关头文件以调用 put 函数,这里暂时用伪代码或注释占位,后续需要包含 endpoint.h 等
 */
void handle_free_entry(struct handle_entry *entry) {
    /* 减少对象引用计数 */
    if (entry->object) {
        /* TODO: 根据类型调用相应的 put 函数
           目前先注释掉,等待集成时添加正确的头文件引用 */
        /*
        switch (entry->type) {
        case HANDLE_ENDPOINT:
            endpoint_put((struct ipc_endpoint *)entry->object);
            break;
        case HANDLE_PHYSMEM:
            physmem_put((struct physmem_region *)entry->object);
            break;
        default:
            break;
        }
        */
    }
    memset(entry, 0, sizeof(struct handle_entry));
}
