#include <arch/cpu.h>

#include <sys/syscall.h>
#include <xnix/abi/perm.h>
#include <xnix/errno.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/string.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

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

    struct abi_profile_create_args kargs;
    int                            ret = copy_from_user(&kargs, user_args, sizeof(kargs));
    if (ret < 0) {
        return ret;
    }

    kargs.name[sizeof(kargs.name) - 1]     = '\0';
    kargs.parent[sizeof(kargs.parent) - 1] = '\0';

    if (kargs.name[0] == '\0') {
        return -EINVAL;
    }
    if (kargs.rule_count > ABI_PERM_RULE_MAX) {
        return -EINVAL;
    }

    /* 检查每条 GRANT 规则都在调用者权限范围内 */
    for (uint32_t i = 0; i < kargs.rule_count; i++) {
        kargs.rules[i].node[ABI_PERM_NODE_MAX - 1] = '\0';
        if (kargs.rules[i].value == 1) { /* GRANT */
            /*
             * 确保权限节点已注册,否则 perm_lookup 会返回 INVALID
             * 导致通配符匹配无法生效(如调用者有 xnix.*,
             * 但 xnix.ipc.* 未注册时 lookup 失败).
             * 注册后 perm_check 会检测到注册表变化并重新解析位图.
             */
            perm_id_t id = perm_lookup(kargs.rules[i].node);
            if (id == PERM_ID_INVALID) {
                id = perm_register(kargs.rules[i].node);
            }
            if (id == PERM_ID_INVALID || !perm_check(proc, id)) {
                return -EPERM;
            }
        }
    }

    /* 检查是否已存在同名 profile */
    if (perm_profile_find(kargs.name)) {
        return -EEXIST;
    }

    /* 创建 profile */
    struct perm_profile *profile = perm_profile_create(kargs.name);
    if (!profile) {
        return -ENOMEM;
    }

    /* 设置继承 */
    if (kargs.parent[0] != '\0') {
        struct perm_profile *parent = perm_profile_find(kargs.parent);
        if (parent) {
            perm_profile_inherit(profile, parent);
        }
    }

    /* 设置规则 */
    for (uint32_t i = 0; i < kargs.rule_count; i++) {
        perm_profile_set(profile, kargs.rules[i].node,
                         kargs.rules[i].value ? PERM_GRANT : PERM_DENY);
    }

    return 0;
}

void sys_perm_init(void) {
    syscall_register(SYS_PERM_CHECK, sys_perm_check, 1, "perm_check");
    syscall_register(SYS_PERM_PROFILE_CREATE, sys_perm_profile_create, 1, "perm_profile_create");
}
