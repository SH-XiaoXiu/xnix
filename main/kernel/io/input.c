/**
 * @file input.c
 * @brief 内核输入队列
 *
 * 提供全局输入缓冲区,kbd 驱动写入翻译后的字符,
 * 用户进程通过 syscall 读取.
 */

#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/signal.h>
#include <xnix/sync.h>

#define INPUT_BUF_SIZE 256

static char           input_buf[INPUT_BUF_SIZE];
static uint16_t       input_head = 0;
static uint16_t       input_tail = 0;
static spinlock_t     input_lock;
static struct thread *input_waiter = NULL;

/* 前台进程 PID(Ctrl+C 发送目标) */
static pid_t foreground_pid = 0;

void input_init(void) {
    spin_init(&input_lock);
}

void input_set_foreground(pid_t pid) {
    foreground_pid = pid;
}

pid_t input_get_foreground(void) {
    return foreground_pid;
}

int input_write(char c) {
    /* Ctrl+C (ETX, 0x03) - 发送 SIGINT 给前台进程 */
    if (c == 3) {
        pid_t target = foreground_pid;

        /* 没有前台进程时,发送给 input_waiter */
        if (target <= 1) {
            spin_lock(&input_lock);
            struct thread *waiter = input_waiter;
            spin_unlock(&input_lock);
            if (waiter && waiter->owner) {
                target = waiter->owner->pid;
            }
        }

        if (target > 1) {
            process_kill(target, SIGINT);
        }
        return 0;
    }

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

        /* 被唤醒后检查信号 */
        process_check_signals();

        spin_lock(&input_lock);
    }

    char c     = input_buf[input_tail];
    input_tail = (input_tail + 1) % INPUT_BUF_SIZE;

    spin_unlock(&input_lock);
    return (unsigned char)c;
}
