#ifndef XNIX_BOOT_H
#define XNIX_BOOT_H

#include <asm/multiboot.h>
#include <xnix/types.h>

/**
 * 保存启动命令行指针
 *
 * 由 boot_init() 在 early 阶段调用,供后续阶段查询启动参数.
 */
void boot_cmdline_set(const char *cmdline);

/**
 * 获取启动命令行中的 key=value
 *
 * @param key 键名(不含 '=')
 * @return 指向内部静态缓冲区的 value;未找到返回 NULL
 */
const char *boot_cmdline_get(const char *key);

/**
 * 判断启动命令行是否包含 key=value
 */
bool boot_cmdline_has_kv(const char *key, const char *value);

/**
 * 从启动命令行读取无符号整数参数
 */
bool boot_cmdline_get_u32(const char *key, uint32_t *out);

/**
 * Framebuffer 信息
 */
struct boot_framebuffer_info {
    uint64_t addr;       /* 物理地址 */
    uint32_t pitch;      /* 每行字节数 */
    uint32_t width;      /* 宽度(像素) */
    uint32_t height;     /* 高度(像素) */
    uint8_t  bpp;        /* 每像素位数 */
    uint8_t  type;       /* 类型: 0=索引色, 1=RGB, 2=EGA文本 */
    uint8_t  red_pos;    /* 红色位位置 */
    uint8_t  red_size;   /* 红色位长度 */
    uint8_t  green_pos;  /* 绿色位位置 */
    uint8_t  green_size; /* 绿色位长度 */
    uint8_t  blue_pos;   /* 蓝色位位置 */
    uint8_t  blue_size;  /* 蓝色位长度 */
};

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

/**
 * 获取启动模块的命令行参数
 * @param index 模块索引
 * @return 命令行字符串指针,无则返回 NULL
 */
const char *boot_get_module_cmdline(uint32_t index);

/**
 * 按名称获取启动模块的命令行参数
 *
 * @param name 模块 name= 值
 * @return 命令行字符串指针,无则返回 NULL
 */
const char *boot_get_module_cmdline_by_name(const char *name);

/**
 * 按名称查找启动模块
 *
 * 遍历所有模块的 cmdline,查找 name=<name> 匹配的模块
 *
 * @param name     模块名称
 * @param out_addr 输出模块起始地址
 * @param out_size 输出模块大小
 * @return 0 成功,<0 失败
 */
int boot_find_module_by_name(const char *name, void **out_addr, uint32_t *out_size);

/**
 * 获取 framebuffer 信息
 * @param info 输出参数
 * @return 0 成功(有 framebuffer),-1 无 framebuffer
 */
int boot_get_framebuffer(struct boot_framebuffer_info *info);

/**
 * 收集启动资源
 *
 * 在 handle 系统初始化后调用,在启动 init 之前.
 */
void boot_handles_collect(void);

/**
 * 为 init 进程直接创建 boot handles
 *
 * 在 init 进程的 handle 表中创建硬件资源 handles(如 framebuffer).
 * 应该在 spawn_core 中 creator == NULL 时调用.
 *
 * @param proc init 进程
 * @return 0 成功, <0 失败
 */
struct process;
int boot_handles_create_for_init(struct process *proc);

#endif
