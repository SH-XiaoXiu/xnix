/*
 * pthread core (1:1 user threads)
 */

#include <pthread.h>
#include <string.h>
#include <xnix/errno.h>
#include <xnix/syscall.h>

#define PTHREAD_MAX_THREADS     64
#define PTHREAD_STACK_POOL_SIZE (512u * 1024u)
#define PTHREAD_STACK_ALIGN     4096u

struct pthread_start_args {
    void *(*start)(void *);
    void    *arg;
    void    *stack_base;
    uint32_t stack_size;
};

struct pthread_thread_info {
    pthread_t tid;
    void     *stack_base;
    uint32_t  stack_size;
    uint8_t   detached;
    uint8_t   used;
};

static uint8_t  g_stack_pool[PTHREAD_STACK_POOL_SIZE] __attribute__((aligned(PTHREAD_STACK_ALIGN)));
static uint32_t g_stack_bitmap[PTHREAD_STACK_POOL_SIZE / PTHREAD_STACK_ALIGN / 32];
static struct pthread_thread_info g_threads[PTHREAD_MAX_THREADS];

static volatile uint32_t g_tbl_lock;

static void tbl_lock(void) {
    while (__sync_lock_test_and_set(&g_tbl_lock, 1)) {
        syscall0(SYS_THREAD_YIELD);
    }
}

static void tbl_unlock(void) {
    __sync_lock_release(&g_tbl_lock);
}

static int stack_alloc(uint32_t size, void **out_base, uint32_t *out_size) {
    if (!out_base || !out_size) {
        return EINVAL;
    }
    if (size < PTHREAD_STACK_MIN) {
        return EINVAL;
    }
    if ((size & (PTHREAD_STACK_ALIGN - 1u)) != 0) {
        return EINVAL;
    }

    uint32_t pages       = size / PTHREAD_STACK_ALIGN;
    uint32_t total_pages = (uint32_t)(PTHREAD_STACK_POOL_SIZE / PTHREAD_STACK_ALIGN);

    tbl_lock();

    uint32_t run   = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < total_pages; i++) {
        uint32_t word = i / 32;
        uint32_t bit  = i % 32;
        uint32_t used = (g_stack_bitmap[word] >> bit) & 1u;

        if (!used) {
            if (run == 0) {
                start = i;
            }
            run++;
            if (run == pages) {
                for (uint32_t j = 0; j < pages; j++) {
                    uint32_t idx = start + j;
                    g_stack_bitmap[idx / 32] |= 1u << (idx % 32);
                }
                tbl_unlock();
                *out_base = (void *)(g_stack_pool + start * PTHREAD_STACK_ALIGN);
                *out_size = size;
                return 0;
            }
        } else {
            run = 0;
        }
    }

    tbl_unlock();
    return ENOMEM;
}

static void stack_free(void *base, uint32_t size) {
    if (!base || !size) {
        return;
    }

    uintptr_t pool = (uintptr_t)g_stack_pool;
    uintptr_t p    = (uintptr_t)base;
    if (p < pool || p >= pool + PTHREAD_STACK_POOL_SIZE) {
        return;
    }

    uint32_t offset = (uint32_t)(p - pool);
    if ((offset & (PTHREAD_STACK_ALIGN - 1u)) != 0) {
        return;
    }
    if ((size & (PTHREAD_STACK_ALIGN - 1u)) != 0) {
        return;
    }

    uint32_t start_page = offset / PTHREAD_STACK_ALIGN;
    uint32_t pages      = size / PTHREAD_STACK_ALIGN;

    tbl_lock();
    for (uint32_t j = 0; j < pages; j++) {
        uint32_t idx = start_page + j;
        g_stack_bitmap[idx / 32] &= ~(1u << (idx % 32));
    }
    tbl_unlock();
}

static struct pthread_thread_info *thread_info_find(pthread_t tid) {
    for (int i = 0; i < PTHREAD_MAX_THREADS; i++) {
        if (g_threads[i].used && g_threads[i].tid == tid) {
            return &g_threads[i];
        }
    }
    return NULL;
}

static int thread_info_add(pthread_t tid, void *stack_base, uint32_t stack_size, int detached) {
    tbl_lock();

    for (int i = 0; i < PTHREAD_MAX_THREADS; i++) {
        if (!g_threads[i].used) {
            g_threads[i].used       = 1;
            g_threads[i].tid        = tid;
            g_threads[i].stack_base = stack_base;
            g_threads[i].stack_size = stack_size;
            g_threads[i].detached   = detached ? 1 : 0;
            tbl_unlock();
            return 0;
        }
    }

    tbl_unlock();
    return ENOMEM;
}

static void thread_info_remove(pthread_t tid) {
    tbl_lock();
    struct pthread_thread_info *info = thread_info_find(tid);
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    tbl_unlock();
}

static int thread_info_mark_detached(pthread_t tid) {
    tbl_lock();
    struct pthread_thread_info *info = thread_info_find(tid);
    if (!info) {
        tbl_unlock();
        return ESRCH;
    }
    info->detached = 1;
    tbl_unlock();
    return 0;
}

static int thread_info_is_detached(pthread_t tid) {
    tbl_lock();
    struct pthread_thread_info *info     = thread_info_find(tid);
    int                         detached = info ? (int)info->detached : 0;
    tbl_unlock();
    return detached;
}

static void pthread_entry_wrapper(void *arg) __attribute__((noreturn));

static void pthread_entry_wrapper(void *arg) {
    struct pthread_start_args *a = (struct pthread_start_args *)arg;
    if (!a || !a->start) {
        pthread_exit((void *)(uintptr_t)EINVAL);
    }

    void *retval = a->start(a->arg);

    pthread_t tid = pthread_self();
    if (tid > 0 && thread_info_is_detached(tid)) {
        stack_free(a->stack_base, a->stack_size);
        thread_info_remove(tid);
    }

    pthread_exit(retval);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *),
                   void *arg) {
    if (!thread || !start) {
        return EINVAL;
    }

    uint32_t stacksize   = PTHREAD_STACK_DEFAULT;
    int      detachstate = PTHREAD_CREATE_JOINABLE;
    if (attr) {
        if (attr->stacksize) {
            stacksize = attr->stacksize;
        }
        detachstate = (int)attr->detachstate;
    }

    if (stacksize < PTHREAD_STACK_MIN || (stacksize & (PTHREAD_STACK_ALIGN - 1u)) != 0) {
        return EINVAL;
    }
    if (detachstate != PTHREAD_CREATE_JOINABLE && detachstate != PTHREAD_CREATE_DETACHED) {
        return EINVAL;
    }

    void    *stack_base = NULL;
    uint32_t stack_size = 0;
    int      err        = stack_alloc(stacksize, &stack_base, &stack_size);
    if (err) {
        return err;
    }

    struct pthread_start_args *start_args = (struct pthread_start_args *)stack_base;
    start_args->start                     = start;
    start_args->arg                       = arg;
    start_args->stack_base                = stack_base;
    start_args->stack_size                = stack_size;

    uintptr_t stack_top = (uintptr_t)stack_base + stack_size;
    stack_top &= ~0xFu;

    int32_t tid = syscall3(SYS_THREAD_CREATE, (uint32_t)(uintptr_t)pthread_entry_wrapper,
                           (uint32_t)(uintptr_t)start_args, (uint32_t)stack_top);
    if (tid < 0) {
        stack_free(stack_base, stack_size);
        return -tid;
    }

    *thread = (pthread_t)tid;

    err = thread_info_add(*thread, stack_base, stack_size, detachstate == PTHREAD_CREATE_DETACHED);
    if (err) {
        return err;
    }

    if (detachstate == PTHREAD_CREATE_DETACHED) {
        err = pthread_detach(*thread);
        if (err) {
            return err;
        }
    }

    return 0;
}

void pthread_exit(void *retval) {
    syscall1(SYS_THREAD_EXIT, (uint32_t)(uintptr_t)retval);
    __builtin_unreachable();
}

int pthread_join(pthread_t thread, void **retval) {
    int32_t ret = syscall2(SYS_THREAD_JOIN, (uint32_t)thread, (uint32_t)(uintptr_t)retval);
    if (ret < 0) {
        return -ret;
    }

    tbl_lock();
    struct pthread_thread_info *info       = thread_info_find(thread);
    void                       *stack_base = info ? info->stack_base : NULL;
    uint32_t                    stack_size = info ? info->stack_size : 0;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    tbl_unlock();

    stack_free(stack_base, stack_size);
    return 0;
}

int pthread_detach(pthread_t thread) {
    int32_t ret = syscall1(SYS_THREAD_DETACH, (uint32_t)thread);
    if (ret < 0) {
        return -ret;
    }
    return thread_info_mark_detached(thread);
}

pthread_t pthread_self(void) {
    return (pthread_t)syscall0(SYS_THREAD_SELF);
}

int pthread_yield(void) {
    int32_t ret = syscall0(SYS_THREAD_YIELD);
    return (ret < 0) ? -ret : 0;
}
