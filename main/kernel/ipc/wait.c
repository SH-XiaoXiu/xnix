/**
 * @file wait.c
 * @brief IPC 多路复用 (wait_any)
 *
 * 等待多个 IPC 对象 (endpoint / event) 中的任意一个就绪.
 */

#include <ipc/endpoint.h>
#include <ipc/event.h>
#include <ipc/wait.h>
#include <xnix/cap.h>
#include <xnix/handle.h>
#include <xnix/ipc.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/thread_def.h>

/**
 * 等待多个 endpoint/event 中的任意一个就绪
 *
 * 事件驱动实现:
 * 1. 首先检查是否有已就绪的对象
 * 2. 为每个对象创建 poll_entry 加入其 poll_queue
 * 3. 阻塞等待
 * 4. 被唤醒后找出就绪的对象
 * 5. 清理所有 poll_entry
 */
handle_t ipc_wait_any(struct ipc_wait_set *set, uint32_t timeout_ms) {
    struct process *proc    = process_current();
    struct thread  *current = sched_current();
    handle_t        result  = HANDLE_INVALID;

    if (!set || set->count == 0 || set->count > IPC_WAIT_MAX || !proc || !current) {
        return HANDLE_INVALID;
    }

    /* 在栈上分配 poll entries */
    struct poll_entry entries[IPC_WAIT_MAX];
    void             *objects[IPC_WAIT_MAX]; /* 对象指针 */
    handle_type_t     types[IPC_WAIT_MAX];   /* 对象类型 */
    uint32_t          valid_count = 0;

    /* 第一遍:查找所有对象,检查是否有已就绪的 */
    for (uint32_t i = 0; i < set->count; i++) {
        handle_t handle = set->handles[i];

        struct handle_entry entry;
        if (handle_acquire(proc, handle, HANDLE_NONE, &entry) < 0) {
            continue;
        }

        if (entry.type == HANDLE_ENDPOINT) {
            /* 检查 recv 权限 */
            if (!(entry.rights & HANDLE_RIGHT_READ) || !cap_check(proc, CAP_IPC_RECV)) {
                handle_object_put(entry.type, entry.object);
                continue;
            }

            struct ipc_endpoint *ep = entry.object;
            spin_lock(&ep->lock);
            if (ep->send_queue != NULL) {
                spin_unlock(&ep->lock);
                pr_debug("[IPC] wait_any ready: tid=%d handle=%d\n", current->tid, handle);
                handle_object_put(entry.type, entry.object);
                for (uint32_t j = 0; j < valid_count; j++) {
                    handle_object_put(types[j], objects[j]);
                }
                return handle;
            }
            spin_unlock(&ep->lock);

            objects[valid_count]           = ep;
            types[valid_count]             = HANDLE_ENDPOINT;
            entries[valid_count].handle    = handle;
            entries[valid_count].waiter    = current;
            entries[valid_count].triggered = false;
            entries[valid_count].next      = NULL;
            valid_count++;
        } else if (entry.type == HANDLE_EVENT) {
            struct ipc_event *event = entry.object;
            spin_lock(&event->lock);
            if (event->pending_bits != 0) {
                spin_unlock(&event->lock);
                pr_debug("[IPC] wait_any ready: tid=%d handle=%d\n", current->tid, handle);
                handle_object_put(entry.type, entry.object);
                for (uint32_t j = 0; j < valid_count; j++) {
                    handle_object_put(types[j], objects[j]);
                }
                return handle;
            }
            spin_unlock(&event->lock);

            objects[valid_count]           = event;
            types[valid_count]             = HANDLE_EVENT;
            entries[valid_count].handle    = handle;
            entries[valid_count].waiter    = current;
            entries[valid_count].triggered = false;
            entries[valid_count].next      = NULL;
            valid_count++;
        } else {
            handle_object_put(entry.type, entry.object);
        }
    }

    if (valid_count == 0) {
        return HANDLE_INVALID;
    }

    /* 第二遍:将 poll_entry 加入各对象的 poll_queue */
    for (uint32_t i = 0; i < valid_count; i++) {
        if (types[i] == HANDLE_ENDPOINT) {
            struct ipc_endpoint *ep = objects[i];
            spin_lock(&ep->lock);
            entries[i].next = ep->poll_queue;
            ep->poll_queue  = &entries[i];
            spin_unlock(&ep->lock);
        } else {
            struct ipc_event *event = objects[i];
            spin_lock(&event->lock);
            entries[i].next    = event->poll_queue;
            event->poll_queue  = &entries[i];
            spin_unlock(&event->lock);
        }
    }

    /* 重新检查一次就绪状态，避免"首次检查为空"与"挂入 poll_queue"之间丢唤醒。 */
    for (uint32_t i = 0; i < valid_count; i++) {
        if (types[i] == HANDLE_ENDPOINT) {
            struct ipc_endpoint *ep = objects[i];
            spin_lock(&ep->lock);
            if (ep->send_queue != NULL) {
                spin_unlock(&ep->lock);
                result = entries[i].handle;
                pr_debug("[IPC] wait_any ready(after-arm): tid=%d handle=%d\n",
                         current->tid, result);
                goto cleanup;
            }
            spin_unlock(&ep->lock);
        } else {
            struct ipc_event *event = objects[i];
            spin_lock(&event->lock);
            if (event->pending_bits != 0) {
                spin_unlock(&event->lock);
                result = entries[i].handle;
                pr_debug("[IPC] wait_any ready(after-arm): tid=%d handle=%d\n",
                         current->tid, result);
                goto cleanup;
            }
            spin_unlock(&event->lock);
        }
    }

    /* 阻塞等待 */
    pr_debug("[IPC] wait_any block: tid=%d count=%d\n", current->tid, set->count);
    if (!sched_block_timeout(current, timeout_ms)) {
        result = HANDLE_INVALID; /* 超时 */
    } else {
        /* 找出被触发的 entry */
        for (uint32_t i = 0; i < valid_count; i++) {
            if (entries[i].triggered) {
                result = entries[i].handle;
                pr_debug("[IPC] wait_any wakeup: tid=%d handle=%d\n", current->tid, result);
                break;
            }
        }
    }

    /* 清理:从所有对象的 poll_queue 中移除 entries */
cleanup:
    for (uint32_t i = 0; i < valid_count; i++) {
        if (types[i] == HANDLE_ENDPOINT) {
            struct ipc_endpoint *ep = objects[i];
            spin_lock(&ep->lock);
            poll_remove(&ep->poll_queue, &entries[i]);
            spin_unlock(&ep->lock);
        } else {
            struct ipc_event *event = objects[i];
            spin_lock(&event->lock);
            poll_remove(&event->poll_queue, &entries[i]);
            spin_unlock(&event->lock);
        }
    }

    for (uint32_t i = 0; i < valid_count; i++) {
        handle_object_put(types[i], objects[i]);
    }

    return result;
}
