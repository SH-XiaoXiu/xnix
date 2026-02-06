#ifndef XNIX_CONSOLE_H
#define XNIX_CONSOLE_H

#include <xnix/types.h>

/**
 * 控制台子系统
 *
 * 作用:
 * - 作为内核输出的 fan-out 层: 将 kputc/kputs 等输出分发到多个后端
 * - 后端可以是直接硬件驱动(如 VGA/Serial),也可以是 IPC/UDM stub
 *
 * 同步/异步模式:
 * - 同步驱动(VGA): putc 时立即调用,保证即时输出
 * - 异步驱动(Serial): 写入 ring buffer,由消费者线程处理
 */

/* 驱动标志 */
#define CONSOLE_SYNC  0 /* 同步驱动,putc 时立即调用 */
#define CONSOLE_ASYNC 1 /* 异步驱动,从 buffer 消费 */

/**
 * 控制台后端
 *
 * 一个 console 对应一个输出后端,由 console 子系统负责 fan-out 调用.
 */
struct console {
    const char *name;
    uint32_t    flags; /* CONSOLE_SYNC / CONSOLE_ASYNC */
    void (*init)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*set_color)(kcolor_t color);
    void (*reset_color)(void);
    void (*clear)(void);
    void (*start_consumer)(void); /* 启动异步消费者线程(可选) */

    /* 链表指针(用于驱动注册链表) */
    struct console *next;
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
 * 输出单字符到同步后端
 */
void console_putc_sync(char c);

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

/**
 * 启动异步消费者线程
 *
 * 遍历所有驱动,调用其 start_consumer 回调.
 * 需要在调度器初始化后调用.
 */
void console_start_consumers(void);

/**
 * 启用异步输出
 *
 * 调用后,对 CONSOLE_ASYNC 驱动的输出将写入 ring buffer.
 * 需要在调度器初始化后调用.
 */
void console_async_enable(void);

/**
 * 从 ring buffer 读取一个字符
 *
 * 供异步消费者线程调用.
 *
 * @param c 输出参数
 * @return 0 成功, -1 缓冲区空
 */
int console_ringbuf_get(char *c);

/**
 * 强制刷新缓冲区
 *
 * 阻塞直到 buffer 为空(所有消费者处理完毕).
 * 用于 panic 等需要确保输出完整的场景.
 */
void console_flush(void);

/**
 * 进入紧急模式
 *
 * 禁用异步输出,所有输出直接写入硬件.
 * 用于 panic 等中断已禁用,调度器不可用的场景.
 */
void console_emergency_mode(void);

#endif
