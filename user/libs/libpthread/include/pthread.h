/*
 * Minimal pthread API for Xnix (1:1 threads)
 */

#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t  pthread_t;
typedef uint32_t pthread_mutex_t;

typedef struct {
    uint32_t detachstate;
    uint32_t stacksize;
} pthread_attr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_STACK_MIN       (4u * 1024u)
#define PTHREAD_STACK_DEFAULT   (8u * 1024u)

int       pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *),
                         void *arg);
void      pthread_exit(void *retval) __attribute__((noreturn));
int       pthread_join(pthread_t thread, void **retval);
int       pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int       pthread_yield(void);

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

#endif /* _PTHREAD_H */
