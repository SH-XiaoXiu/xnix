/**
 * @file ringbuf.c
 * @brief 环形缓冲区实现
 */

#include <xnix/ringbuf.h>

void ringbuf_init(struct ringbuf *rb, char *buf, uint32_t size) {
    rb->buf  = buf;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    spin_init(&rb->lock);
}

int ringbuf_put(struct ringbuf *rb, char c) {
    uint32_t next = (rb->head + 1) % rb->size;
    if (next == rb->tail) {
        return -1; /* 满 */
    }
    rb->buf[rb->head] = c;
    rb->head          = next;
    return 0;
}

int ringbuf_get(struct ringbuf *rb, char *c) {
    if (rb->head == rb->tail) {
        return -1; /* 空 */
    }
    *c       = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return 0;
}

uint32_t ringbuf_used(struct ringbuf *rb) {
    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }
    return rb->size - rb->tail + rb->head;
}

uint32_t ringbuf_free(struct ringbuf *rb) {
    return rb->size - 1 - ringbuf_used(rb);
}

bool ringbuf_empty(struct ringbuf *rb) {
    return rb->head == rb->tail;
}

bool ringbuf_full(struct ringbuf *rb) {
    return ((rb->head + 1) % rb->size) == rb->tail;
}
