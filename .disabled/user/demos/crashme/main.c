/**
 * @file crashme.c
 * @brief 异常处理测试程序
 *
 * 用于测试内核的用户态异常处理机制.
 * 启动后延时 3 秒触发除零异常.
 */

#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("[crashme] Starting exception test...\n");

    /* 延时 3 秒,让用户看到输出 */
    sleep(3);

    printf("[crashme] Triggering division by zero NOW!\n");

    /* 触发除零异常 */
    volatile int x = 1;
    volatile int y = 0;
    volatile int z = x / y;
    (void)z;

    /* 不应该执行到这里 */
    printf("[crashme] ERROR: Should not reach here!\n");
    return 1;
}
