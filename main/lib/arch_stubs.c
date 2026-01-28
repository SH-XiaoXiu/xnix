/**
 * @file arch_stubs.c
 * @brief 架构函数的弱符号默认实现
 *
 * 提供架构相关函数的默认空实现,由具体架构的强符号覆盖.
 * 这允许内核代码无条件调用这些函数,而无需使用条件编译.
 */

#include <xnix/types.h>

/**
 * SMP 初始化
 *
 * 默认实现:不执行任何操作(单核系统或未启用 SMP)
 * x86 实现在 arch/x86/smp_init.c 中提供强符号覆盖
 */
__attribute__((weak)) void arch_smp_init(void) {
    /* 默认空实现 */
}
