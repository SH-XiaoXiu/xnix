#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/string.h>

/* 全局 Profile 注册表 */
static struct {
    struct perm_profile *profiles[PERM_MAX_PROFILES];
    uint32_t             count;
    spinlock_t           lock;
} g_profiles = {0};

/**
 * 初始化 Profile 系统
 *
 * Profile 定义已移至用户态配置(core_services.conf 的 [profile.*] 段).
 * init 进程的权限在 process_spawn_init() 中通过 perm_grant(state, "xnix.*") 设置,
 * 不需要命名 profile.
 *
 * init 启动后通过 SYS_PERM_PROFILE_CREATE 注册所有 profile.
 */
void perm_profile_init(void) {
    spin_init(&g_profiles.lock);
    g_profiles.count = 0;
}

/**
 * 创建新权限 Profile
 */
struct perm_profile *perm_profile_create(const char *name) {
    struct perm_profile *profile = kmalloc(sizeof(struct perm_profile));
    if (!profile) {
        return NULL;
    }

    strncpy(profile->name, name, sizeof(profile->name) - 1);
    profile->name[sizeof(profile->name) - 1] = '\0';
    profile->parent                          = NULL;
    profile->perms                           = NULL;
    profile->perm_count                      = 0;
    profile->perm_capacity                   = 0;

    spin_lock(&g_profiles.lock);
    if (g_profiles.count < PERM_MAX_PROFILES) {
        g_profiles.profiles[g_profiles.count++] = profile;
    } else {
        spin_unlock(&g_profiles.lock);
        kfree(profile);
        return NULL;
    }
    spin_unlock(&g_profiles.lock);

    return profile;
}

/**
 * 在 Profile 中设置权限
 */
int perm_profile_set(struct perm_profile *profile, const char *node, perm_value_t value) {
    if (!profile || !node) {
        return -1; // EINVAL
    }

    /* 需要时扩展容量 */
    if (profile->perm_count >= profile->perm_capacity) {
        uint32_t new_cap   = profile->perm_capacity == 0 ? 16 : profile->perm_capacity * 2;
        void    *new_perms = krealloc(profile->perms, new_cap * sizeof(struct perm_entry));
        if (!new_perms) {
            return -1; // ENOMEM
        }

        profile->perms         = new_perms;
        profile->perm_capacity = new_cap;
    }

    /* 添加权限 */
    profile->perms[profile->perm_count].node  = kstrdup(node);
    profile->perms[profile->perm_count].value = value;
    profile->perm_count++;

    return 0;
}

/**
 * 设置 Profile 继承
 */
int perm_profile_inherit(struct perm_profile *child, struct perm_profile *parent) {
    if (!child) {
        return -1; // EINVAL
    }

    /* 检查循环继承 */
    struct perm_profile *p = parent;
    while (p) {
        if (p == child) {
            return -1; /* 循环 */
        }
        p = p->parent;
    }

    child->parent = parent;
    return 0;
}

/**
 * 按名称查找 Profile
 */
struct perm_profile *perm_profile_find(const char *name) {
    if (!name) {
        return NULL;
    }

    spin_lock(&g_profiles.lock);
    for (uint32_t i = 0; i < g_profiles.count; i++) {
        if (strcmp(g_profiles.profiles[i]->name, name) == 0) {
            struct perm_profile *profile = g_profiles.profiles[i];
            spin_unlock(&g_profiles.lock);
            return profile;
        }
    }
    spin_unlock(&g_profiles.lock);

    return NULL;
}
