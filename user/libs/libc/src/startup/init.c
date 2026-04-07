/**
 * @file init.c
 * @brief libc 初始化
 *
 * 在 main() 之前由 crt0 调用以初始化 libc 服务
 */

#include <stdio_internal.h>
#include <xnix/env.h>
#include <xnix/fd.h>

/**
 * 初始化 libc 服务
 *
 * 此函数在 main() 之前由 crt0.s 调用.
 * 初始化 fd 表(绑定 stdio handle 到 fd 0/1/2),然后初始化标准流.
 */
void __libc_init(int argc, char **argv) {
    const char *argv0 = NULL;

    if (argc > 0 && argv) {
        argv0 = argv[0];
    }

    __env_init_process_name(argv0);
    fd_table_init();
    _libc_stdio_init();
}

void __libc_fini(void) {
    _libc_stdio_fini();
    fd_table_fini();
}
