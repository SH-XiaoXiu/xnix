/*
 * pthread 测试
 */

#include <pthread.h>
#include <stdio.h>

static int             g_counter = 0;
static pthread_mutex_t g_mutex   = 0;

/* 测试 1: 基础创建和 join */
static void *simple_thread(void *arg) {
    int id = (int)(uintptr_t)arg;
    printf("  thread %d running (tid=%d)\n", id, pthread_self());
    return (void *)(uintptr_t)(id + 100);
}

static void test_basic(void) {
    printf("\n[test 1] basic create and join\n");

    pthread_t t;
    pthread_create(&t, NULL, simple_thread, (void *)1);

    void *ret;
    pthread_join(t, &ret);

    int result = (int)(uintptr_t)ret;
    printf("  joined, got %d\n", result);
}

/* 测试 2: 多线程 */
static void test_multiple(void) {
    printf("\n[test 2] multiple threads\n");

    pthread_t threads[3];
    for (int i = 0; i < 3; i++) {
        pthread_create(&threads[i], NULL, simple_thread, (void *)(uintptr_t)(i + 1));
    }

    for (int i = 0; i < 3; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        printf("  thread %d returned %d\n", i + 1, (int)(uintptr_t)ret);
    }
}

/* 测试 3: mutex */
static void *increment_thread(void *arg) {
    int use_lock = (int)(uintptr_t)arg;

    for (int i = 0; i < 100; i++) {
        if (use_lock) {
            pthread_mutex_lock(&g_mutex);
        }

        g_counter++;

        if (use_lock) {
            pthread_mutex_unlock(&g_mutex);
        }

        pthread_yield();
    }

    return NULL;
}

static void test_mutex(void) {
    printf("\n[test 3] mutex\n");

    /* 先跑一遍不加锁的，看看会不会出问题 */
    printf("  without lock:\n");
    g_counter = 0;

    pthread_t t[3];
    for (int i = 0; i < 3; i++) {
        pthread_create(&t[i], NULL, increment_thread, (void *)0);
    }
    for (int i = 0; i < 3; i++) {
        pthread_join(t[i], NULL);
    }

    printf("    counter = %d (expected 300)\n", g_counter);

    /* 加锁的版本 */
    printf("  with lock:\n");
    g_counter = 0;
    pthread_mutex_init(&g_mutex, NULL);

    for (int i = 0; i < 3; i++) {
        pthread_create(&t[i], NULL, increment_thread, (void *)1);
    }
    for (int i = 0; i < 3; i++) {
        pthread_join(t[i], NULL);
    }

    printf("    counter = %d (expected 300)\n", g_counter);
    pthread_mutex_destroy(&g_mutex);
}

/* 测试 4: detached */
static void *detached_func(void *arg) {
    (void)arg;
    printf("  detached thread running\n");
    return NULL;
}

static void test_detached(void) {
    printf("\n[test 4] detached thread\n");

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_t t;
    pthread_create(&t, &attr, detached_func, NULL);
    pthread_attr_destroy(&attr);

    /* 等一下让它跑完 */
    for (int i = 0; i < 50; i++) {
        pthread_yield();
    }

    printf("  (no need to join)\n");
}

/* 测试 5: 错误情况 */
static void *dummy(void *arg) {
    (void)arg;
    for (int i = 0; i < 20; i++) {
        pthread_yield();
    }
    return NULL;
}

static void test_errors(void) {
    printf("\n[test 5] error cases\n");

    /* join 自己 */
    pthread_t self = pthread_self();
    int       err  = pthread_join(self, NULL);
    printf("  join self: err=%d (EDEADLK=35)\n", err);

    /* 重复 join */
    pthread_t t;
    pthread_create(&t, NULL, dummy, NULL);
    pthread_join(t, NULL);
    err = pthread_join(t, NULL);
    printf("  double join: err=%d (EINVAL=22)\n", err);
}

int main(void) {
    printf("pthread test starting...\n");

    test_basic();
    test_multiple();
    test_mutex();
    test_detached();
    test_errors();

    printf("\nAll tests done\n");
    return 0;
}
