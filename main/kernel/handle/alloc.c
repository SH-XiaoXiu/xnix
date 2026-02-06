#include <arch/mmu.h>

#include <ipc/endpoint.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/physmem.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* 前向声明 */
static void handle_free_entry(struct handle_entry *entry);

/**
 * 分配 Handle
 *
 * @param proc    在哪个进程中分配
 * @param type    Handle 类型
 * @param object  内核对象指针
 * @param name    可选的名称(可为 NULL)
 * @return Handle ID,失败返回 HANDLE_INVALID
 */
handle_t handle_alloc(struct process *proc, handle_type_t type, void *object, const char *name) {
    if (!proc || !proc->handles || !object) {
        return HANDLE_INVALID;
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    /* 寻找空闲槽位 */
    handle_t h = HANDLE_INVALID;
    for (uint32_t i = 0; i < table->capacity; i++) {
        if (table->entries[i].type == HANDLE_NONE) {
            h = i;
            break;
        }
    }

    /* 如果需要则扩展容量 */
    if (h == HANDLE_INVALID) {
        uint32_t new_cap     = table->capacity * 2;
        void    *new_entries = krealloc(table->entries, new_cap * sizeof(struct handle_entry));
        if (!new_entries) {
            spin_unlock(&table->lock);
            return HANDLE_INVALID;
        }
        table->entries = new_entries;
        memset(&table->entries[table->capacity], 0,
               (new_cap - table->capacity) * sizeof(struct handle_entry));
        h               = table->capacity;
        table->capacity = new_cap;
    }

    /* 初始化表项 */
    struct handle_entry *entry = &table->entries[h];
    entry->type                = type;
    entry->object              = object;
    if (name) {
        strncpy(entry->name, name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
    } else {
        entry->name[0] = '\0';
    }

    /* 缓存权限 ID(用于加速 IPC 检查) */
    if (type == HANDLE_ENDPOINT) {
        char        perm_send[64], perm_recv[64];
        const char *ep_name = (name && name[0]) ? name : "unknown";
        snprintf(perm_send, sizeof(perm_send), "xnix.ipc.endpoint.%s.send", ep_name);
        snprintf(perm_recv, sizeof(perm_recv), "xnix.ipc.endpoint.%s.recv", ep_name);
        entry->perm_send = perm_register(perm_send);
        entry->perm_recv = perm_register(perm_recv);
    } else {
        entry->perm_send = PERM_ID_INVALID;
        entry->perm_recv = PERM_ID_INVALID;
    }

    spin_unlock(&table->lock);
    return h;
}

/**
 * 在指定槽位分配 Handle(Hint)
 */
handle_t handle_alloc_at(struct process *proc, handle_type_t type, void *object, const char *name,
                         handle_t hint) {
    if (hint == HANDLE_INVALID) {
        return handle_alloc(proc, type, object, name);
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    /* 如果需要则扩展容量 */
    if (hint >= table->capacity) {
        uint32_t new_cap = hint + 16;
        /* 确保容量是 2 的幂次或合理增长 */
        if (new_cap < table->capacity * 2) {
            new_cap = table->capacity * 2;
        }

        void *new_entries = krealloc(table->entries, new_cap * sizeof(struct handle_entry));
        if (!new_entries) {
            spin_unlock(&table->lock);
            return HANDLE_INVALID;
        }
        table->entries = new_entries;
        memset(&table->entries[table->capacity], 0,
               (new_cap - table->capacity) * sizeof(struct handle_entry));
        table->capacity = new_cap;
    }

    /* 检查槽位是否空闲 */
    if (table->entries[hint].type != HANDLE_NONE) {
        spin_unlock(&table->lock);
        return handle_alloc(proc, type, object, name); /* 自动分配 */
    }

    /* 初始化表项 */
    struct handle_entry *entry = &table->entries[hint];
    entry->type                = type;
    entry->object              = object;
    if (name) {
        strncpy(entry->name, name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
    } else {
        entry->name[0] = '\0';
    }

    /* 缓存权限 ID(用于加速 IPC 检查) */
    if (type == HANDLE_ENDPOINT) {
        char        perm_send[64], perm_recv[64];
        const char *ep_name = (name && name[0]) ? name : "unknown";
        snprintf(perm_send, sizeof(perm_send), "xnix.ipc.endpoint.%s.send", ep_name);
        snprintf(perm_recv, sizeof(perm_recv), "xnix.ipc.endpoint.%s.recv", ep_name);
        entry->perm_send = perm_register(perm_send);
        entry->perm_recv = perm_register(perm_recv);
    } else {
        entry->perm_send = PERM_ID_INVALID;
        entry->perm_recv = PERM_ID_INVALID;
    }

    spin_unlock(&table->lock);
    return hint;
}

/**
 * 释放 Handle
 */
void handle_free(struct process *proc, handle_t h) {
    if (!proc || !proc->handles) {
        return;
    }

    struct handle_table *table = proc->handles;
    spin_lock(&table->lock);

    if (h < table->capacity && table->entries[h].type != HANDLE_NONE) {
        handle_free_entry(&table->entries[h]);
    }

    spin_unlock(&table->lock);
}

/**
 * 释放 Handle 表项资源
 */
static void handle_free_entry(struct handle_entry *entry) {
    /* 减少对象引用计数 */
    if (entry->object) {
        switch (entry->type) {
        case HANDLE_ENDPOINT:
            endpoint_unref((struct ipc_endpoint *)entry->object);
            break;
        case HANDLE_PHYSMEM:
            physmem_put((struct physmem_region *)entry->object);
            break;
        default:
            break;
        }
    }
    entry->type    = HANDLE_NONE;
    entry->object  = NULL;
    entry->name[0] = '\0';
}
