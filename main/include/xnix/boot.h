#ifndef XNIX_BOOT_H
#define XNIX_BOOT_H

#include <asm/multiboot.h>
#include <xnix/types.h>

/**
 * 启动策略入口
 *
 * 负责在内核非常早期确定"平台能力/启动策略"的最终结果,例如:
 * - 探测并填充 g_hal_features
 * - 从 bootloader 读取 RAM 大小/模块信息
 * - 应用启动参数覆盖(如强制关闭 MMU/SMP)
 *
 * 可替换性:
 * - 默认实现位于 kernel/boot/boot.c,并以 weak 形式提供
 * - 若想在构建时替换(裸机/固定配置等),在你的平台代码中提供同名的 boot_init()
 *   强符号实现即可覆盖默认实现
 */
void boot_init(uint32_t magic, const struct multiboot_info *mb_info);

/**
 * 返回启动时选择的 init module 索引(默认 0)
 *
 * 默认实现会尝试从 cmdline 读取 xnix.initmod=N 并写入该值.
 * 若替换了 boot_init(),也可以自行维护该返回值.
 */
uint32_t boot_get_initmod_index(void);

uint32_t boot_get_serialmod_index(void);

/**
 * 获取启动模块数量
 */
uint32_t boot_get_module_count(void);

/**
 * 获取启动模块信息
 * @param index 模块索引
 * @param out_addr 输出模块起始物理地址
 * @param out_size 输出模块大小
 * @return 0 成功,<0 失败
 */
int boot_get_module(uint32_t index, void **out_addr, uint32_t *out_size);

#endif
