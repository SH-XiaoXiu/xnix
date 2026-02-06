#include <arch/mmu.h>
#include <kernel/ipc/endpoint.h>
#include <kernel/process/process.h>
#include <xnix/handle.h>
#include <xnix/physmem.h>
#include <xnix/string.h>

/* 前向声明:内部函数 */
struct handle_entry *handle_get_entry(struct handle_table *table, handle_t h);

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

    /* 获取源表项 */
    struct handle_entry *src_entry = handle_get_entry(src->handles, src_h);
    if (!src_entry) {
        return HANDLE_INVALID;
    }

    /* 增加对象引用计数 */
    void         *object = src_entry->object;
    handle_type_t type   = src_entry->type;

    switch (type) {
    case HANDLE_ENDPOINT:
        endpoint_ref((struct ipc_endpoint *)object);
        break;
    case HANDLE_PHYSMEM:
        physmem_get((struct physmem_region *)object);
        break;
    default:
        break;
    }

    /* 在目标进程分配 */
    const char *dst_name = name ? name : src_entry->name;
    handle_t    dst_h    = handle_alloc_at(dst, type, object, dst_name, dst_hint);

    if (dst_h == HANDLE_INVALID) {
        /* 分配失败,回滚引用计数 */
        switch (type) {
        case HANDLE_ENDPOINT:
            endpoint_unref((struct ipc_endpoint *)object);
            break;
        case HANDLE_PHYSMEM:
            physmem_put((struct physmem_region *)object);
            break;
        default:
            break;
        }
    }

    return dst_h;
}
