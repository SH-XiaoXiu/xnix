#include <ipc/endpoint.h>
#include <ipc/notification.h>
#include <xnix/handle.h>
#include <xnix/physmem.h>

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
    case HANDLE_NOTIFICATION:
        notification_ref(object);
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
    case HANDLE_NOTIFICATION:
        notification_unref(object);
        break;
    default:
        break;
    }
}
