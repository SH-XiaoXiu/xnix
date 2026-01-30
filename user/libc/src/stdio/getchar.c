/**
 * @file getchar.c
 * @brief 标准输入函数
 */

#include <stdio.h>
#include <xnix/syscall.h>

int getchar(void) {
    return sys_input_read();
}
