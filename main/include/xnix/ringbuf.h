/**
 * @file ringbuf.h
 * @brief 环形缓冲区
 */

#ifndef XNIX_RINGBUF_H
#define XNIX_RINGBUF_H

#include <xnix/sync.h>
#include <xnix/types.h>

struct ringbuf {
    char      *buf;
    uint32_t   size;
    uint32_t   head; /* 写入位置 */
    uint32_t   tail; /* 读取位置 */
    spinlock_t lock;
};

/**
 * 初始化环形缓冲区
 *
 * @param rb   缓冲区结构
 * @param buf  数据存储区
 * @param size 存储区大小
 */
void ringbuf_init(struct ringbuf *rb, char *buf, uint32_t size);

/**
 * 写入一个字符
 *
 * @return 0 成功, -1 缓冲区满
 */
int ringbuf_put(struct ringbuf *rb, char c);

/**
 * 读取一个字符
 *
 * @param c 输出参数
 * @return 0 成功, -1 缓冲区空
 */
int ringbuf_get(struct ringbuf *rb, char *c);

/**
 * 查询已用空间
 */
uint32_t ringbuf_used(struct ringbuf *rb);

/**
 * 查询剩余空间
 */
uint32_t ringbuf_free(struct ringbuf *rb);

/**
 * 检查是否为空
 */
bool ringbuf_empty(struct ringbuf *rb);

/**
 * 检查是否已满
 */
bool ringbuf_full(struct ringbuf *rb);

#endif
