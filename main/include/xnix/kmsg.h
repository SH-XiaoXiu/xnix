/**
 * @file kmsg.h
 * @brief 内核日志环形缓冲 (类似 Linux dmesg)
 *
 * 持久化存储内核日志条目,用户态可通过 SYS_KMSG_READ 读取.
 * 每条日志包含序列号,时间戳,级别和文本内容.
 */

#ifndef XNIX_KMSG_H
#define XNIX_KMSG_H

#include <xnix/types.h>

/* 缓冲区大小 */
#define KMSG_BUF_SIZE CFG_KMSG_BUF_SIZE
#define KMSG_MAX_LINE CFG_KMSG_MAX_LINE

/* 日志设施 */
#define KMSG_KERN   0 /* 内核核心 */
#define KMSG_DRIVER 1 /* 驱动程序 */
#define KMSG_MM     2 /* 内存管理 */
#define KMSG_SCHED  3 /* 调度器 */

/**
 * kmsg 条目头(存储在环形缓冲中)
 *
 * 布局: [header][text bytes][NUL]
 * 每条日志在缓冲中占用 sizeof(header) + len + 1 字节
 */
struct kmsg_entry {
    uint32_t seq;       /* 单调递增序列号 */
    uint32_t timestamp; /* boot ticks */
    uint8_t  level;     /* LOG_ERR..LOG_DBG */
    uint8_t  facility;  /* KMSG_KERN, KMSG_DRIVER... */
    uint16_t len;       /* 文本长度(不含 NUL) */
};

/**
 * 初始化 kmsg 子系统
 *
 * 在 boot_phase_early() 中尽早调用.
 */
void kmsg_init(void);

/**
 * 写入一条内核日志
 *
 * @param level    日志级别 (LOG_ERR..LOG_DBG)
 * @param facility 日志设施 (KMSG_KERN 等)
 * @param fmt      格式字符串
 */
void kmsg_log(int level, int facility, const char *fmt, ...);

/**
 * 写入预格式化的内核日志
 *
 * @param level    日志级别
 * @param facility 日志设施
 * @param text     已格式化的文本
 * @param len      文本长度
 */
void kmsg_log_raw(int level, int facility, const char *text, uint16_t len);

/**
 * 从 kmsg 缓冲读取条目
 *
 * @param seq  输入/输出:当前序列号,读取后更新为下一条的序列号
 * @param buf  输出缓冲区
 * @param size 缓冲区大小
 * @return 读取的字节数,-1 表示无更多条目,-2 表示缓冲区太小
 *
 * 输出格式: "<level>,<seq>,<timestamp>;text\n"
 */
int kmsg_read(uint32_t *seq, char *buf, size_t size);

/**
 * 获取当前最新序列号
 */
uint32_t kmsg_get_seq(void);

#endif /* XNIX_KMSG_H */
