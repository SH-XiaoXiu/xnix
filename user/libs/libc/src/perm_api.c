/**
 * @file perm_api.c
 * @brief 用户态权限操作高层 API 实现
 */

#include <errno.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/perm.h>
#include <xnix/perm_api.h>
#include <xnix/syscall.h>

int perm_has(const char *node) {
    if (!node) {
        errno = EINVAL;
        return -1;
    }

    /* 查询所有已授权权限并检查是否包含 node */
    struct abi_perm_info buf[64];
    int count = sys_perm_query(buf, 64);
    if (count < 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(buf[i].node, node) == 0 && buf[i].granted) {
            return 1;
        }
    }
    return 0;
}

int perm_grant_to(pid_t pid, const char *node) {
    return sys_perm_grant_to(pid, node);
}

int perm_revoke_from(pid_t pid, const char *node) {
    return sys_perm_revoke_from(pid, node);
}

int perm_list(void (*cb)(const char *node, int granted, void *ctx), void *ctx) {
    if (!cb) {
        errno = EINVAL;
        return -1;
    }

    struct abi_perm_info buf[64];
    int count = sys_perm_query(buf, 64);
    if (count < 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        cb(buf[i].node, (int)buf[i].granted, ctx);
    }
    return count;
}

int handle_list_fds(void (*cb)(uint32_t h, int type, uint32_t rights,
                               const char *name, void *ctx), void *ctx) {
    if (!cb) {
        errno = EINVAL;
        return -1;
    }

    struct abi_handle_info buf[64];
    int count = sys_handle_list(buf, 64);
    if (count < 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        cb(buf[i].handle, (int)buf[i].type, buf[i].rights, buf[i].name, ctx);
    }
    return count;
}
