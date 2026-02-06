/**
 * @file init.c
 * @brief libc 初始化
 *
 * 在 main() 之前由 crt0 调用以初始化 libc 服务
 */

#include <stdio_internal.h>

/**
 * 初始化 libc 服务
 *
 * 此函数在 main() 之前由 crt0.s 调用.
 * 查找 tty endpoint 并初始化标准流(stdin/stdout/stderr).
 */
void __libc_init(void) {
    _libc_stdio_init();
}
