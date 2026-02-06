/**
 * @file main.c
 * @brief 内存分配测试程序
 *
 * 用法:
 *   memtest        - 交互模式,按 Enter 分配内存
 *   memtest -a     - 自动模式,每秒自动分配内存
 *   memtest -a 10  - 自动模式,分配 10 次后退出
 *   memtest -a -q  - 自动静默模式(适合后台运行)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xnix/syscall.h>

#define ALLOC_SIZE (16UL * 1024UL) /* 每次分配 16KB */
#define MAX_ALLOCS 64              /* 最多分配 64 次 = 1MB */

static int simple_atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

int main(int argc, char **argv) {
    void *ptrs[MAX_ALLOCS];
    int   count     = 0;
    int   auto_mode = 0;
    int   quiet     = 0;
    int   max_count = MAX_ALLOCS;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--auto") == 0) {
            auto_mode = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            max_count = simple_atoi(argv[i]);
            if (max_count > MAX_ALLOCS) {
                max_count = MAX_ALLOCS;
            }
        }
    }

    if (!quiet) {
        printf("Memory Allocation Test (PID %d)\n", sys_getpid());
    }

    if (auto_mode) {
        if (!quiet) {
            printf("Auto mode: allocating %d KB every second, max %d times\n\n", ALLOC_SIZE / 1024,
                   max_count);
        }

        while (count < max_count) {
            /* 分配内存 */
            void *p = malloc(ALLOC_SIZE);
            if (!p) {
                if (!quiet) {
                    printf("malloc failed at %d KB!\n", (count * ALLOC_SIZE) / 1024);
                }
                break;
            }

            /* 写入数据确保页面被实际分配 */
            memset(p, 0xAA, ALLOC_SIZE);
            ptrs[count++] = p;

            if (!quiet) {
                printf("[%2d] Heap: %4d KB\n", count, (count * ALLOC_SIZE) / 1024);
                fflush(NULL);
            }

            sys_sleep(1000);
        }

        if (!quiet) {
            printf("\nMax reached. Holding memory for 10 seconds...\n");
        }
        sys_sleep(10000);

    } else {
        printf("Interactive mode: Press Enter to allocate %d KB\n", ALLOC_SIZE / 1024);
        printf("  'q' = quit, 'f' = free all\n\n");

        while (count < max_count) {
            printf("[%2d] Heap: %4d KB | Press Enter...", count, (count * ALLOC_SIZE) / 1024);
            fflush(NULL);

            int c = getchar();
            if (c == 'q' || c == 'Q') {
                printf("\nQuitting...\n");
                break;
            }
            if (c == 'f' || c == 'F') {
                printf("\nFreeing all memory...\n");
                for (int i = 0; i < count; i++) {
                    free(ptrs[i]);
                }
                count = 0;
                printf("Done.\n\n");
                continue;
            }

            void *p = malloc(ALLOC_SIZE);
            if (!p) {
                printf("\nmalloc failed!\n");
                break;
            }

            memset(p, 0xAA, ALLOC_SIZE);
            ptrs[count++] = p;
            printf("\n");
        }
    }

    if (!quiet) {
        printf("Freeing %d KB...\n", (count * ALLOC_SIZE) / 1024);
    }
    for (int i = 0; i < count; i++) {
        free(ptrs[i]);
    }

    if (!quiet) {
        printf("Done.\n");
    }
    return 0;
}
