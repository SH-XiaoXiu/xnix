/**
 * @file perm_api.c
 * @brief 用户态能力与 handle 自省 API 实现
 */

#include <errno.h>
#include <xnix/abi/handle.h>
#include <xnix/perm_api.h>
#include <xnix/syscall.h>

int cap_has(uint32_t cap) {
    uint32_t mask = sys_cap_query();
    return (mask & cap) == cap;
}

uint32_t cap_query(void) {
    return sys_cap_query();
}

int handle_list_fds(void (*cb)(uint32_t h, int type, uint32_t rights,
                               const char *name, void *ctx), void *ctx) {
    if (!cb) {
        errno = EINVAL;
        return -1;
    }

    struct abi_handle_info buf[64] = {0};
    int count = sys_handle_list(buf, 64);
    if (count < 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        cb(buf[i].handle, (int)buf[i].type, buf[i].rights, buf[i].name, ctx);
    }
    return count;
}
