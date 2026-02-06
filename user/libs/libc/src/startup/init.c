/**
 * @file init.c
 * @brief libc 初始化
 *
 * 在 main() 之前由 crt0 调用以初始化 libc 服务
 */

/* 对 libserial 的弱引用(可选链接) */
extern int serial_init(void) __attribute__((weak));

/**
 * 初始化 libc 服务
 *
 * 此函数在 main() 之前由 crt0.s 调用
 *
 * 注意:为了避免引导循环依赖,serial_init() 不会被自动调用.
 * 程序应在串行端点可用时显式调用 serial_init().
 */
void __libc_init(void) {
    /* 无默认初始化:由程序自行决定何时初始化输出/VFS 等服务 */
}
