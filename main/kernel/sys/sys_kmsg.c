/**
 * @file kernel/sys/sys_kmsg.c
 * @brief SYS_KMSG_READ 系统调用
 *
 * 允许用户态读取内核日志缓冲(kmsg),需要 xnix.kernel.kmsg 权限.
 */

#include <sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/kmsg.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>

/**
 * SYS_KMSG_READ: 读取内核日志条目
 *
 * ebx = seq_ptr (用户态指向 uint32_t 的指针,输入/输出序列号)
 * ecx = buf     (用户态缓冲区)
 * edx = size    (缓冲区大小)
 *
 * 返回: 读取的字节数,-EPERM 无权限,-EINVAL 参数错误,
 *       -ENOENT 无更多条目,-ENOSPC 缓冲区太小
 */
static int32_t sys_kmsg_read(const uint32_t *args) {
    uint32_t *seq_ptr = (uint32_t *)args[0];
    char     *buf     = (char *)args[1];
    uint32_t  size    = args[2];

    if (!seq_ptr || !buf || size == 0) {
        return -EINVAL;
    }

    /* 权限检查 */
    struct process *proc = (struct process *)process_current();
    if (!perm_check_name(proc, "xnix.kernel.kmsg")) {
        return -EPERM;
    }

    /* 读取用户态的 seq 值 */
    uint32_t seq = *seq_ptr;

    int ret = kmsg_read(&seq, buf, size);
    if (ret == -1) {
        return -ENOENT;
    }
    if (ret == -2) {
        return -ENOSPC;
    }

    /* 写回更新后的序列号 */
    *seq_ptr = seq;
    return ret;
}

void sys_kmsg_init(void) {
    syscall_register(SYS_KMSG_READ, sys_kmsg_read, 3, "kmsg_read");
}
