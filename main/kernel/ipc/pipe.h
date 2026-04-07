#ifndef KERNEL_IPC_PIPE_H
#define KERNEL_IPC_PIPE_H

#include <ipc/wait.h>
#include <xnix/abi/handle.h>
#include <xnix/sync.h>
#include <xnix/types.h>

struct thread;

#define PIPE_BUF_SIZE 4096

/**
 * 内核管道对象
 *
 * 字节流通道: 有内核缓冲区, 支持阻塞读写, EOF/EPIPE 语义.
 * 关闭写端 → 读者收到 EOF (read 返回 0).
 * 关闭读端 → 写者收到 EPIPE (write 返回 -EPIPE).
 */
struct ipc_pipe {
    spinlock_t     lock;
    uint8_t        buf[PIPE_BUF_SIZE];
    uint32_t       head;
    uint32_t       tail;
    uint32_t       refcount;
    uint32_t       reader_count;
    uint32_t       writer_count;
    struct thread *read_queue;
    struct thread *write_queue;
    struct poll_entry *poll_queue;
};

/**
 * 创建管道, 返回读写两端 handle
 */
int pipe_create(handle_t *read_h, handle_t *write_h);

/**
 * 从管道读取数据
 * @return 读取字节数, 0=EOF, 负数=错误
 */
int pipe_read(struct ipc_pipe *pipe, void *ubuf, uint32_t size);

/**
 * 向管道写入数据
 * @return 写入字节数, 负数=错误 (-EPIPE)
 */
int pipe_write(struct ipc_pipe *pipe, const void *ubuf, uint32_t size);

void pipe_ref(void *ptr);
void pipe_unref(void *ptr);
void pipe_open_read(void *ptr);   /* ref + reader_count++ */
void pipe_open_write(void *ptr);  /* ref + writer_count++ */
void pipe_close_read(void *ptr);  /* reader_count-- + 唤醒 + unref */
void pipe_close_write(void *ptr); /* writer_count-- + 唤醒 + unref */

#endif /* KERNEL_IPC_PIPE_H */
