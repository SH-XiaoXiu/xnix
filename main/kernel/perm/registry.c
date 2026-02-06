#include <xnix/abi/perm.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>

/* 全局权限注册表 */
static struct perm_registry {
    struct perm_node *nodes;
    uint32_t          count;
    uint32_t          capacity;
    spinlock_t        lock;
} g_registry = {0};

/* 辅助函数:字符串哈希 (FNV-1a) */
static uint32_t hash_string(const char *s) {
    uint32_t hash = 2166136261u;
    while (*s) {
        hash ^= (uint8_t)*s++;
        hash *= 16777619u;
    }
    return hash;
}

/* 辅助函数:计算点号数量(用于深度计算) */
static uint16_t count_dots(const char *s) {
    uint16_t count = 0;
    while (*s) {
        if (*s == '.') {
            count++;
        }
        s++;
    }
    return count;
}

/**
 * 初始化权限子系统
 */
void perm_init(void) {
    perm_registry_init();
    perm_profile_init();
}

/**
 * 初始化权限注册表
 */
void perm_registry_init(void) {
    g_registry.capacity = 1024; /* 最大 1024 个权限节点 */
    g_registry.count    = 0;
    g_registry.nodes    = kmalloc(sizeof(struct perm_node) * g_registry.capacity);
    spin_init(&g_registry.lock);

    /* 注册内置权限 */
    perm_register(PERM_NODE_IPC_SEND);
    perm_register(PERM_NODE_IPC_RECV);
    perm_register(PERM_NODE_IPC_ENDPOINT_CREATE);
    perm_register(PERM_NODE_IO_PORT_ALL);
    perm_register(PERM_NODE_PROCESS_SPAWN);
    perm_register(PERM_NODE_PROCESS_EXEC);
    perm_register(PERM_NODE_HANDLE_GRANT);
    perm_register(PERM_NODE_MM_MMAP);
    perm_register("xnix.irq.all");
    perm_register("xnix.debug.console");
    perm_register("xnix.kernel.kmsg");
}

/**
 * 注册权限节点(幂等)
 *
 * @param name  权限节点名称(如 "xnix.ipc.send")
 * @return 权限 ID,失败返回 PERM_ID_INVALID
 */
perm_id_t perm_register(const char *name) {
    if (!name || strlen(name) >= PERM_NODE_NAME_MAX) {
        return PERM_ID_INVALID;
    }

    spin_lock(&g_registry.lock);

    /* 检查是否已注册 */
    for (uint32_t i = 0; i < g_registry.count; i++) {
        if (strcmp(g_registry.nodes[i].name, name) == 0) {
            perm_id_t id = g_registry.nodes[i].id;
            spin_unlock(&g_registry.lock);
            return id;
        }
    }

    /* 分配新 ID */
    if (g_registry.count >= g_registry.capacity) {
        spin_unlock(&g_registry.lock);
        kprintf("ERROR: Permission registry full\n");
        return PERM_ID_INVALID;
    }

    perm_id_t         id   = g_registry.count++;
    struct perm_node *node = &g_registry.nodes[id];

    node->id    = id;
    node->name  = kstrdup(name); /* 驻留字符串 */
    node->hash  = hash_string(name);
    node->depth = count_dots(name);

    spin_unlock(&g_registry.lock);
    return id;
}

/**
 * 按名称查找权限 ID
 *
 * @param name  权限节点名称
 * @return 权限 ID,未找到返回 PERM_ID_INVALID
 */
perm_id_t perm_lookup(const char *name) {
    if (!name) {
        return PERM_ID_INVALID;
    }

    uint32_t hash = hash_string(name);

    spin_lock(&g_registry.lock);
    for (uint32_t i = 0; i < g_registry.count; i++) {
        if (g_registry.nodes[i].hash == hash && strcmp(g_registry.nodes[i].name, name) == 0) {
            perm_id_t id = g_registry.nodes[i].id;
            spin_unlock(&g_registry.lock);
            return id;
        }
    }
    spin_unlock(&g_registry.lock);

    return PERM_ID_INVALID;
}

const char *perm_get_name(perm_id_t id) {
    spin_lock(&g_registry.lock);
    if (id >= g_registry.count) {
        spin_unlock(&g_registry.lock);
        return NULL;
    }
    const char *name = g_registry.nodes[id].name;
    spin_unlock(&g_registry.lock);
    return name;
}

uint32_t perm_registry_count(void) {
    return g_registry.count;
}
