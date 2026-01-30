/*
 * pthread attributes (minimal)
 */

#include <pthread.h>
#include <xnix/errno.h>

int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr) {
        return EINVAL;
    }
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    attr->stacksize   = PTHREAD_STACK_DEFAULT;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    if (!attr) {
        return EINVAL;
    }
    attr->detachstate = 0;
    attr->stacksize   = 0;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
    if (!attr) {
        return EINVAL;
    }
    if (stacksize < PTHREAD_STACK_MIN) {
        return EINVAL;
    }
    if ((stacksize & (4096u - 1u)) != 0) {
        return EINVAL;
    }
    attr->stacksize = (uint32_t)stacksize;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
    if (!attr || !stacksize) {
        return EINVAL;
    }
    *stacksize = (size_t)attr->stacksize;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
    if (!attr) {
        return EINVAL;
    }
    if (detachstate != PTHREAD_CREATE_JOINABLE && detachstate != PTHREAD_CREATE_DETACHED) {
        return EINVAL;
    }
    attr->detachstate = (uint32_t)detachstate;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
    if (!attr || !detachstate) {
        return EINVAL;
    }
    *detachstate = (int)attr->detachstate;
    return 0;
}
