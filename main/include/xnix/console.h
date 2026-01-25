#ifndef XNIX_CONSOLE_H
#define XNIX_CONSOLE_H

#include <xnix/capability.h>
#include <xnix/types.h>

/**
 * 控制台子系统
 *
 * 作用:
 * - 作为内核输出的 fan-out 层: 将 kputc/kputs 等输出分发到多个后端
 * - 后端可以是直接硬件驱动(如 VGA/Serial),也可以是 IPC/UDM stub
 */

/**
 * 控制台 UDM/IPC 协议
 *
 * 约定:
 * - msg.regs.data[0] 为 opcode
 * - 其余参数放在 data[1..] 中
 * - 当前只覆盖最小控制台功能(putc/颜色/清屏)
 */
#define CONSOLE_UDM_OPS(X) \
    X(PUTC, 1)             \
    X(SET_COLOR, 2)        \
    X(RESET_COLOR, 3)      \
    X(CLEAR, 4)

enum console_udm_op {
#define CONSOLE_UDM_OP_ENUM(name, val) CONSOLE_UDM_OP_##name = (val),
    CONSOLE_UDM_OPS(CONSOLE_UDM_OP_ENUM)
#undef CONSOLE_UDM_OP_ENUM
};

/**
 * 控制台后端
 *
 * 一个 console 对应一个输出后端,由 console 子系统负责 fan-out 调用.
 */
struct console {
    const char *name;
    void (*init)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*set_color)(kcolor_t color);
    void (*reset_color)(void);
    void (*clear)(void);
};

/**
 * 注册一个 console 后端
 *
 * @param c 后端对象(静态或长期驻留内存)
 * @return 0 成功, -1 失败
 */
int console_register(struct console *c);

/**
 * 用新后端替换同名后端
 *
 * @param name 要替换的后端名
 * @param c    新后端对象
 * @return 0 成功, -1 失败(未找到/参数非法)
 */
int console_replace(const char *name, struct console *c);

/**
 * 初始化已注册的所有后端
 */
void console_init(void);

/**
 * 输出单字符到所有后端
 */
void console_putc(char c);

/**
 * 输出字符串到所有后端
 */
void console_puts(const char *s);

/**
 * 设置所有后端颜色
 */
void console_set_color(kcolor_t color);

/**
 * 重置所有后端颜色
 */
void console_reset_color(void);

/**
 * 清屏(对所有后端调用 clear)
 */
void console_clear(void);

#endif
