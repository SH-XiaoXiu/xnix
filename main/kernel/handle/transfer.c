#include <arch/mmu.h>

#include <xnix/handle.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/**
 * 将 Handle 传递给另一个进程
 *
 * @param src       源进程
 * @param src_h     源进程中的 Handle
 * @param dst       目标进程
 * @param name      在目标进程中的名称(可为 NULL 以保持原名)
 * @param dst_hint  目标进程中的期望槽位(HANDLE_INVALID 表示自动分配)
 * @return 目标进程中的 Handle,失败返回 HANDLE_INVALID
 */
handle_t handle_transfer(struct process *src, handle_t src_h, struct process *dst, const char *name,
                         handle_t dst_hint) {
    if (!src || !src->handles || !dst || !dst->handles) {
        return HANDLE_INVALID;
    }

    struct handle_entry src_entry;
    if (handle_acquire(src, src_h, HANDLE_NONE, &src_entry) < 0) {
        return HANDLE_INVALID;
    }

    /* 在目标进程分配 */
    const char *dst_name = name ? name : src_entry.name;
    handle_t    dst_h = handle_alloc_at(dst, src_entry.type, src_entry.object, dst_name, dst_hint);

    if (dst_h == HANDLE_INVALID) {
        handle_object_put(src_entry.type, src_entry.object);
    }

    pr_debug("[HANDLE] transfer: %d:%d -> %d:%d type=%d name=%s\n", src->pid, src_h, dst->pid,
             dst_h, src_entry.type, dst_name);

    return dst_h;
}
