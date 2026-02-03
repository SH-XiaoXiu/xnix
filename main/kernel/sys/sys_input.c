/**
 * @file kernel/sys/sys_input.c
 * @brief 输入相关系统调用
 *
 * DEPRECATED: 输入队列处理和前台进程管理已移至用户空间 kbd 驱动.
 * 此文件保留为空桩,未来可能完全移除.
 */

#include <kernel/sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/syscall.h>
#include <xnix/types.h>

/**
 * 输入子系统初始化 (空操作)
 */
void input_init(void) {
    /* 输入处理已移至用户态 kbd 驱动 */
}

void sys_input_init(void) {
    /* 不再注册任何输入相关的系统调用 */
}
