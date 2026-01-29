#include <pthread.h>
#include <stdio.h>

static void *worker_thread(void *arg) {
    int id = (int)(uintptr_t)arg;

    pthread_t tid = pthread_self();
    printf("[pthread_test] thread start: id=%d tid=%d\n", id, tid);
    for (int i = 0; i < 100; i++) {
        printf("[pthread_test] id=%d tid=%d i=%d\n", id, tid, i);
    }
    printf("[pthread_test] thread finish: id=%d tid=%d\n", id, tid);

    return (void *)(uintptr_t)id;
}

int main(void) {
    printf("[pthread_test] start\n");

    pthread_t threads[5];
    for (int i = 0; i < 5; i++) {
        int err = pthread_create(&threads[i], NULL, worker_thread, (void *)(uintptr_t)(i + 1));
        if (err) {
            printf("[pthread_test] pthread_create failed: %d\n", err);
            return 1;
        }
    }

    for (int i = 0; i < 5; i++) {
        void *retval = NULL;
        int   err    = pthread_join(threads[i], &retval);
        if (err) {
            printf("[pthread_test] pthread_join failed: %d\n", err);
            return 1;
        }
        printf("[pthread_test] join ok: tid=%d retval=%d\n", threads[i], (int)(uintptr_t)retval);
    }

    printf("[pthread_test] done\n");
    return 0;
}
