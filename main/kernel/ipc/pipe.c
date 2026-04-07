/**
 * @file pipe.c
 * @brief 内核管道原语
 *
 * 字节流通道, 带内核缓冲区和阻塞语义.
 * 关闭写端触发原子 EOF, 关闭读端触发原子 EPIPE.
 */

#include <arch/cpu.h>

#include <ipc/pipe.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/thread_def.h>
#include <xnix/usraccess.h>

/* ---- 环形缓冲区 ---- */

static uint32_t ring_used(struct ipc_pipe *p) {
    return (p->tail - p->head + PIPE_BUF_SIZE) % PIPE_BUF_SIZE;
}

static uint32_t ring_free(struct ipc_pipe *p) {
    return PIPE_BUF_SIZE - 1 - ring_used(p);
}

/* ---- 引用计数 ---- */

void pipe_ref(void *ptr) {
    struct ipc_pipe *p = ptr;
    uint32_t flags;
    if (!p) return;
    flags = cpu_irq_save();
    p->refcount++;
    cpu_irq_restore(flags);
}


void pipe_unref(void *ptr) {
    struct ipc_pipe *p = ptr;
    uint32_t flags;
    if (!p) return;
    flags = cpu_irq_save();
    p->refcount--;
    if (p->refcount == 0) {
        cpu_irq_restore(flags);
        kfree(p);
        return;
    }
    cpu_irq_restore(flags);
}

void pipe_open_read(void *ptr) {
    struct ipc_pipe *p = ptr;
    uint32_t flags;
    if (!p) return;
    flags = cpu_irq_save();
    p->refcount++;
    p->reader_count++;
    cpu_irq_restore(flags);
}

void pipe_open_write(void *ptr) {
    struct ipc_pipe *p = ptr;
    uint32_t flags;
    if (!p) return;
    flags = cpu_irq_save();
    p->refcount++;
    p->writer_count++;
    cpu_irq_restore(flags);
}

/* ---- 唤醒等待队列中的所有线程 ---- */

static void wakeup_all(struct thread **queue) {
    struct thread *t = *queue;
    *queue = NULL;
    while (t) {
        struct thread *next = t->wait_next;
        t->wait_next = NULL;
        sched_wakeup_thread(t);
        t = next;
    }
}

static void enqueue_thread(struct thread **queue, struct thread *t) {
    t->wait_next = *queue;
    *queue = t;
}

static void dequeue_thread(struct thread **queue, struct thread *t) {
    struct thread **pp = queue;
    while (*pp) {
        if (*pp == t) {
            *pp = t->wait_next;
            t->wait_next = NULL;
            return;
        }
        pp = &(*pp)->wait_next;
    }
}

/* ---- 关闭端 ---- */

void pipe_close_read(void *ptr) {
    struct ipc_pipe *p = ptr;
    if (!p) return;

    spin_lock(&p->lock);
    if (p->reader_count > 0) {
        p->reader_count--;
    }
    if (p->reader_count == 0) {
        /* 唤醒所有阻塞写者, 他们将收到 -EPIPE */
        wakeup_all(&p->write_queue);
    }
    spin_unlock(&p->lock);

    pipe_unref(p);
}

void pipe_close_write(void *ptr) {
    struct ipc_pipe *p = ptr;
    if (!p) return;

    spin_lock(&p->lock);
    if (p->writer_count > 0) {
        p->writer_count--;
    }
    if (p->writer_count == 0) {
        /* 唤醒所有阻塞读者, 他们将收到 EOF (返回 0) */
        wakeup_all(&p->read_queue);
        /* 唤醒 poll 等待者 */
        poll_wakeup(p->poll_queue);
    }
    spin_unlock(&p->lock);

    pipe_unref(p);
}

/* ---- 创建 ---- */

int pipe_create(handle_t *read_h, handle_t *write_h) {
    struct process  *proc = process_current();
    struct ipc_pipe *p;
    handle_t rh, wh;

    if (!proc) return -EINVAL;

    p = kzalloc(sizeof(*p));
    if (!p) return -ENOMEM;

    spin_init(&p->lock);
    p->refcount     = 2; /* 读端 + 写端 各持有一个引用 */
    p->reader_count = 1;
    p->writer_count = 1;

    rh = handle_alloc(proc, HANDLE_PIPE_READ, p, NULL);
    if (rh == HANDLE_INVALID) {
        kfree(p);
        return -ENOMEM;
    }

    wh = handle_alloc(proc, HANDLE_PIPE_WRITE, p, NULL);
    if (wh == HANDLE_INVALID) {
        handle_free(proc, rh);
        pipe_close_read(p);
        return -ENOMEM;
    }

    *read_h  = rh;
    *write_h = wh;
    return 0;
}

/* ---- 读 ---- */

int pipe_read(struct ipc_pipe *p, void *ubuf, uint32_t size) {
    struct thread *current = sched_current();
    uint32_t       total   = 0;

    if (!p || !ubuf || size == 0) return -EINVAL;

    spin_lock(&p->lock);

    for (;;) {
        uint32_t avail = ring_used(p);

        if (avail > 0) {
            /* 有数据, 尽可能多读 */
            uint32_t n = avail;
            if (n > size - total) n = size - total;

            /* 从环形缓冲区拷贝到用户空间 */
            for (uint32_t i = 0; i < n; i++) {
                uint8_t byte = p->buf[p->head];
                p->head = (p->head + 1) % PIPE_BUF_SIZE;

                spin_unlock(&p->lock);
                int ret = copy_to_user((uint8_t *)ubuf + total + i, &byte, 1);
                spin_lock(&p->lock);

                if (ret < 0) {
                    /* 唤醒写者(已腾出空间) */
                    wakeup_all(&p->write_queue);
                    spin_unlock(&p->lock);
                    return total > 0 ? (int)total : ret;
                }
            }

            total += n;

            /* 唤醒阻塞写者(已腾出空间) */
            wakeup_all(&p->write_queue);

            /* 已读够请求的量, 返回 */
            spin_unlock(&p->lock);
            return (int)total;
        }

        /* 缓冲区空 */
        if (total > 0) {
            /* 已经读到一些数据, 返回 */
            spin_unlock(&p->lock);
            return (int)total;
        }

        if (p->writer_count == 0) {
            /* 写端全部关闭 → EOF */
            spin_unlock(&p->lock);
            return 0;
        }

        /* 阻塞等待数据 */
        enqueue_thread(&p->read_queue, current);
        spin_unlock(&p->lock);

        if (!sched_block_timeout(current, 0)) {
            /* 超时(不应发生, timeout=0 表示永久等待) */
            spin_lock(&p->lock);
            dequeue_thread(&p->read_queue, current);
            spin_unlock(&p->lock);
            return -ETIMEDOUT;
        }

        spin_lock(&p->lock);
    }
}

/* ---- 写 ---- */

int pipe_write(struct ipc_pipe *p, const void *ubuf, uint32_t size) {
    struct thread *current = sched_current();
    uint32_t       total   = 0;

    if (!p || !ubuf || size == 0) return -EINVAL;

    spin_lock(&p->lock);

    for (;;) {
        if (p->reader_count == 0) {
            /* 读端全部关闭 → EPIPE */
            spin_unlock(&p->lock);
            return -EPIPE;
        }

        uint32_t space = ring_free(p);
        if (space > 0) {
            uint32_t n = size - total;
            if (n > space) n = space;

            /* 从用户空间拷贝到环形缓冲区 */
            for (uint32_t i = 0; i < n; i++) {
                uint8_t byte;
                spin_unlock(&p->lock);
                int ret = copy_from_user(&byte, (const uint8_t *)ubuf + total + i, 1);
                spin_lock(&p->lock);

                if (ret < 0) {
                    wakeup_all(&p->read_queue);
                    poll_wakeup(p->poll_queue);
                    spin_unlock(&p->lock);
                    return total > 0 ? (int)total : ret;
                }

                p->buf[p->tail] = byte;
                p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
            }

            total += n;

            /* 唤醒阻塞读者(有数据了) */
            wakeup_all(&p->read_queue);
            poll_wakeup(p->poll_queue);

            if (total >= size) {
                spin_unlock(&p->lock);
                return (int)total;
            }

            /* 还有更多数据要写, 继续循环 */
            continue;
        }

        /* 缓冲区满, 阻塞等待空间 */
        enqueue_thread(&p->write_queue, current);
        spin_unlock(&p->lock);

        if (!sched_block_timeout(current, 0)) {
            spin_lock(&p->lock);
            dequeue_thread(&p->write_queue, current);
            spin_unlock(&p->lock);
            return total > 0 ? (int)total : -ETIMEDOUT;
        }

        spin_lock(&p->lock);
    }
}
