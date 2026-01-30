/*
 * pthread mutex (syscall-backed)
 */

#include <pthread.h>
#include <xnix/errno.h>
#include <xnix/syscall.h>

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    (void)attr;
    if (!mutex) {
        return EINVAL;
    }

    int32_t handle = syscall0(SYS_MUTEX_CREATE);
    if (handle < 0) {
        return -handle;
    }

    *mutex = (uint32_t)handle;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex) {
        return EINVAL;
    }

    int32_t ret = syscall1(SYS_MUTEX_DESTROY, *mutex);
    if (ret < 0) {
        return -ret;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex) {
        return EINVAL;
    }

    int32_t ret = syscall1(SYS_MUTEX_LOCK, *mutex);
    return (ret < 0) ? -ret : 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) {
        return EINVAL;
    }

    int32_t ret = syscall1(SYS_MUTEX_UNLOCK, *mutex);
    return (ret < 0) ? -ret : 0;
}
