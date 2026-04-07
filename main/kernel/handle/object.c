#include <ipc/endpoint.h>
#include <ipc/event.h>
#include <ipc/pipe.h>
#include <xnix/handle.h>
#include <xnix/physmem.h>

void process_watch_ref(void *ptr);
void process_watch_unref(void *ptr);

void handle_object_get(handle_type_t type, void *object) {
    if (!object) {
        return;
    }

    switch (type) {
    case HANDLE_ENDPOINT:
        endpoint_ref(object);
        break;
    case HANDLE_PHYSMEM:
        physmem_get(object);
        break;
    case HANDLE_EVENT:
        event_ref(object);
        break;
    case HANDLE_PROC_WATCH:
        process_watch_ref(object);
        break;
    case HANDLE_PIPE_READ:
    case HANDLE_PIPE_WRITE:
        pipe_ref(object);
        break;
    default:
        break;
    }
}

void handle_object_put(handle_type_t type, void *object) {
    if (!object) {
        return;
    }

    switch (type) {
    case HANDLE_ENDPOINT:
        endpoint_unref(object);
        break;
    case HANDLE_PHYSMEM:
        physmem_put(object);
        break;
    case HANDLE_EVENT:
        event_unref(object);
        break;
    case HANDLE_PROC_WATCH:
        process_watch_unref(object);
        break;
    case HANDLE_PIPE_READ:
    case HANDLE_PIPE_WRITE:
        /* 临时引用释放: 只减 refcount, 不触发 EOF/EPIPE */
        pipe_unref(object);
        break;
    default:
        break;
    }
}

/**
 * 新 handle 被创建时调用 (alloc / transfer)
 * 对 pipe: 增加 reader/writer count
 * 对其他类型: 等同于 handle_object_get
 */
void handle_object_open(handle_type_t type, void *object) {
    if (!object) {
        return;
    }

    switch (type) {
    case HANDLE_PIPE_READ:
        pipe_open_read(object);
        return;
    case HANDLE_PIPE_WRITE:
        pipe_open_write(object);
        return;
    default:
        handle_object_get(type, object);
        return;
    }
}

/**
 * handle 从表中永久移除时调用
 * 对 pipe: 减少 reader/writer count 并触发 EOF/EPIPE
 */
void handle_object_destroy(handle_type_t type, void *object) {
    if (!object) {
        return;
    }

    switch (type) {
    case HANDLE_PIPE_READ:
        pipe_close_read(object);
        return;
    case HANDLE_PIPE_WRITE:
        pipe_close_write(object);
        return;
    default:
        /* 其他类型的 destroy 和 put 行为一致 */
        handle_object_put(type, object);
        return;
    }
}
