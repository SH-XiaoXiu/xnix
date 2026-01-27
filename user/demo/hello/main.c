/**
 * @file hello/main.c
 * @brief Hello World 示例程序
 */

#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("[hello] Hello from user space!\n");

    for (int i = 1; i <= 3; i++) {
        sleep(2);
        printf("[hello] tick %d\n", i);
    }

    printf("[hello] Goodbye!\n");
    return 0;
}
