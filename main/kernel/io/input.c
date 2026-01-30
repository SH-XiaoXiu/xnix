/**
 * @file input.c
 * @brief 内核输入队列
 *
 * 提供全局输入缓冲区,kbd 驱动写入翻译后的字符,
 * 用户进程通过 syscall 读取.
 */

#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/sync.h>

#define INPUT_BUF_SIZE 256

static char           input_buf[INPUT_BUF_SIZE];
static uint16_t       input_head = 0;
static uint16_t       input_tail = 0;
static spinlock_t     input_lock;
static struct thread *input_waiter = NULL;

void input_init(void) {
    spin_init(&input_lock);
}

int input_write(char c) {
    spin_lock(&input_lock);

    uint16_t next = (input_head + 1) % INPUT_BUF_SIZE;
    if (next == input_tail) {
        /* 缓冲区满 */
        spin_unlock(&input_lock);
        return -1;
    }

    input_buf[input_head] = c;
    input_head            = next;

    /* 唤醒等待的读者 */
    if (input_waiter) {
        sched_wakeup_thread(input_waiter);
        input_waiter = NULL;
    }

    spin_unlock(&input_lock);
    return 0;
}

int input_read(void) {
    spin_lock(&input_lock);

    while (input_head == input_tail) {
        /* 缓冲区空,阻塞等待 */
        input_waiter = sched_current();
        spin_unlock(&input_lock);

        sched_block(&input_buf);

        spin_lock(&input_lock);
    }

    char c     = input_buf[input_tail];
    input_tail = (input_tail + 1) % INPUT_BUF_SIZE;

    spin_unlock(&input_lock);
    return (unsigned char)c;
}
