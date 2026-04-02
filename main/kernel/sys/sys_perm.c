#include <arch/cpu.h>

#include <sys/syscall.h>
#include <xnix/abi/perm.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/string.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>
#include <xnix/stdio.h>

/* SYS_PERM_CHECK: ebx=perm_id */
static int32_t sys_perm_check(const uint32_t *args) {
    perm_id_t       id   = (perm_id_t)args[0];
    struct process *proc = (struct process *)process_current();

    if (perm_check(proc, id)) {
        return 0;
    }
    return -EPERM;
}

/* SYS_PERM_PROFILE_CREATE: ebx=abi_profile_create_args* */
static int32_t sys_perm_profile_create(const uint32_t *args) {
    struct abi_profile_create_args *user_args =
        (struct abi_profile_create_args *)(uintptr_t)args[0];
    struct process *proc = (struct process *)process_current();

    /* abi_profile_create_args 含 rules[64],太大不能放栈上 */
    struct abi_profile_create_args *kargs = kmalloc(sizeof(*kargs));
    if (!kargs) {
        return -ENOMEM;
    }

    int ret = copy_from_user(kargs, user_args, sizeof(*kargs));
    if (ret < 0) {
        kfree(kargs);
        return ret;
    }

    kargs->name[sizeof(kargs->name) - 1]     = '\0';
    kargs->parent[sizeof(kargs->parent) - 1] = '\0';

    if (kargs->name[0] == '\0') {
        kfree(kargs);
        return -EINVAL;
    }
    if (kargs->rule_count > ABI_PERM_RULE_MAX) {
        kfree(kargs);
        return -EINVAL;
    }

    /* 检查每条 GRANT 规则都在调用者权限范围内 */
    for (uint32_t i = 0; i < kargs->rule_count; i++) {
        kargs->rules[i].node[ABI_PERM_NODE_MAX - 1] = '\0';
        if (kargs->rules[i].value == 1) { /* GRANT */
            perm_id_t id = perm_lookup(kargs->rules[i].node);
            if (id == PERM_ID_INVALID) {
                id = perm_register(kargs->rules[i].node);
            }
            if (id == PERM_ID_INVALID || !perm_check(proc, id)) {
                kfree(kargs);
                return -EPERM;
            }
        }
    }

    /* 检查是否已存在同名 profile */
    if (perm_profile_find(kargs->name)) {
        kfree(kargs);
        return -EEXIST;
    }

    /* 创建 profile */
    struct perm_profile *profile = perm_profile_create(kargs->name);
    if (!profile) {
        kfree(kargs);
        return -ENOMEM;
    }

    /* 设置继承 */
    if (kargs->parent[0] != '\0') {
        struct perm_profile *parent = perm_profile_find(kargs->parent);
        if (parent) {
            perm_profile_inherit(profile, parent);
        }
    }

    /* 设置规则 */
    for (uint32_t i = 0; i < kargs->rule_count; i++) {
        perm_profile_set(profile, kargs->rules[i].node,
                         kargs->rules[i].value ? PERM_GRANT : PERM_DENY);
    }

    kfree(kargs);
    return 0;
}

/* 公共辅助: 从用户空间读取权限节点名 */
static int read_perm_node(const char *user_ptr, char *buf, size_t buf_size) {
    if (!user_ptr) {
        return -EINVAL;
    }
    for (size_t i = 0; i < buf_size; i++) {
        char ch;
        int  ret = copy_from_user(&ch, user_ptr + i, 1);
        if (ret < 0) {
            return ret;
        }
        buf[i] = ch;
        if (ch == '\0') {
            return 0;
        }
    }
    buf[buf_size - 1] = '\0';
    return 0;
}

/* SYS_PERM_GRANT: ebx=pid, ecx=node_ptr */
static int32_t sys_perm_grant_to(const uint32_t *args) {
    pid_t           pid      = (pid_t)args[0];
    const char     *node_ptr = (const char *)(uintptr_t)args[1];
    struct process *caller   = (struct process *)process_current();

    /* 检查调用者有委托权限 */
    if (!perm_check_name(caller, PERM_NODE_PERM_DELEGATE)) {
        return -EPERM;
    }

    char node_buf[PERM_NODE_NAME_MAX];
    int  ret = read_perm_node(node_ptr, node_buf, sizeof(node_buf));
    if (ret < 0) {
        return ret;
    }

    /* 降权约束: 调用者必须自己拥有该权限才能委托 */
    perm_id_t id = perm_lookup(node_buf);
    if (id == PERM_ID_INVALID) {
        id = perm_register(node_buf);
    }
    if (id == PERM_ID_INVALID || !perm_check(caller, id)) {
        return -EPERM;
    }

    struct process *target = process_find_by_pid(pid);
    if (!target) {
        return -ENOENT;
    }

    ret = perm_grant(target->perms, node_buf);
    process_unref(target);
    return ret;
}

/* SYS_PERM_REVOKE: ebx=pid, ecx=node_ptr */
static int32_t sys_perm_revoke_from(const uint32_t *args) {
    pid_t           pid      = (pid_t)args[0];
    const char     *node_ptr = (const char *)(uintptr_t)args[1];
    struct process *caller   = (struct process *)process_current();

    /* 检查调用者有委托权限 */
    if (!perm_check_name(caller, PERM_NODE_PERM_DELEGATE)) {
        return -EPERM;
    }

    char node_buf[PERM_NODE_NAME_MAX];
    int  ret = read_perm_node(node_ptr, node_buf, sizeof(node_buf));
    if (ret < 0) {
        return ret;
    }

    struct process *target = process_find_by_pid(pid);
    if (!target) {
        return -ENOENT;
    }

    ret = perm_deny(target->perms, node_buf);
    process_unref(target);
    return ret;
}

/* SYS_PERM_QUERY: ebx=buf, ecx=max_count, 返回实际写入条数 */
static int32_t sys_perm_query(const uint32_t *args) {
    struct abi_perm_info *user_buf   = (struct abi_perm_info *)(uintptr_t)args[0];
    uint32_t              max_count  = args[1];
    struct process       *proc       = (struct process *)process_current();

    if (!user_buf || max_count == 0) {
        return -EINVAL;
    }

    uint32_t total   = perm_registry_count();
    uint32_t written = 0;

    for (uint32_t id = 0; id < total && written < max_count; id++) {
        if (!perm_check(proc, (perm_id_t)id)) {
            continue;
        }
        const char *name = perm_get_name((perm_id_t)id);
        if (!name) {
            continue;
        }

        struct abi_perm_info info;
        info.granted = 1;
        strncpy(info.node, name, sizeof(info.node) - 1);
        info.node[sizeof(info.node) - 1] = '\0';

        int ret = copy_to_user(&user_buf[written], &info, sizeof(info));
        if (ret < 0) {
            return ret;
        }
        written++;
    }

    return (int32_t)written;
}

/* SYS_PERM_PROFILE_ADD_RULES: ebx=profile_name, ecx=rules_ptr, edx=count */
static int32_t sys_perm_profile_add_rules(const uint32_t *args) {
    const char              *name_ptr  = (const char *)(uintptr_t)args[0];
    struct abi_perm_rule    *rules_ptr = (struct abi_perm_rule *)(uintptr_t)args[1];
    uint32_t                 count     = args[2];
    struct process          *proc      = (struct process *)process_current();

    if (!name_ptr || !rules_ptr || count == 0 || count > ABI_PERM_RULE_MAX) {
        return -EINVAL;
    }

    char name_buf[32];
    int  ret = copy_from_user(name_buf, name_ptr, sizeof(name_buf));
    if (ret < 0) {
        return ret;
    }
    name_buf[sizeof(name_buf) - 1] = '\0';

    struct perm_profile *profile = perm_profile_find(name_buf);
    if (!profile) {
        return -ENOENT;
    }

    /* 逐条读取并验证规则 */
    for (uint32_t i = 0; i < count; i++) {
        struct abi_perm_rule rule;
        ret = copy_from_user(&rule, &rules_ptr[i], sizeof(rule));
        if (ret < 0) {
            return ret;
        }
        rule.node[ABI_PERM_NODE_MAX - 1] = '\0';

        /* GRANT 规则需要降权检查 */
        if (rule.value == 1) {
            perm_id_t id = perm_lookup(rule.node);
            if (id == PERM_ID_INVALID) {
                id = perm_register(rule.node);
            }
            if (id == PERM_ID_INVALID || !perm_check(proc, id)) {
                return -EPERM;
            }
        }

        perm_profile_set(profile, rule.node, rule.value ? PERM_GRANT : PERM_DENY);
    }

    return 0;
}

void sys_perm_init(void) {
    syscall_register(SYS_PERM_CHECK, sys_perm_check, 1, "perm_check");
    syscall_register(SYS_PERM_PROFILE_CREATE, sys_perm_profile_create, 1, "perm_profile_create");
    syscall_register(SYS_PERM_GRANT, sys_perm_grant_to, 2, "perm_grant");
    syscall_register(SYS_PERM_REVOKE, sys_perm_revoke_from, 2, "perm_revoke");
    syscall_register(SYS_PERM_QUERY, sys_perm_query, 2, "perm_query");
    syscall_register(SYS_PERM_PROFILE_ADD_RULES, sys_perm_profile_add_rules, 3,
                     "perm_profile_add_rules");
}
