#include <arch/cpu.h>

#include <asm/irq_defs.h>
#include <kernel/capability/capability.h>
#include <kernel/io/ioport.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

#include "xnix/errno.h"

/*
 * 系统调用处理函数
 * 参数来自寄存器:
 * eax: 系统调用号
 * ebx: 参数 1
 * ecx: 参数 2
 * edx: 参数 3
 * esi: 参数 4
 * edi: 参数 5
 *
 * 返回值通过 eax 返回
 */
/* 声明 thread_exit */
extern void thread_exit(int code);

/*
 * IPC syscall 的用户指针处理
 *
 * 原则:
 * - syscall 边界必须把用户指针复制进内核缓冲区,IPC 内核实现只处理内核指针
 * - 避免 IPC 路径直接触碰用户内存导致页故障/越界
 *
 * 说明:
 * - caps 传递暂未实现,这里将 caps.count 清零
 * - 长消息 buffer 目前采用"复制"策略(kmalloc + copy_from_user/copy_to_user)
 */
static int syscall_ipc_msg_copy_in(struct ipc_message **out_kmsg, struct ipc_message *user_msg,
                                   bool copy_buffer) {
    if (!out_kmsg || !user_msg) {
        return -EINVAL;
    }

    struct ipc_message umsg;
    int                ret = copy_from_user(&umsg, user_msg, sizeof(umsg));
    if (ret < 0) {
        return ret;
    }

    struct ipc_message *kmsg = kzalloc(sizeof(*kmsg));
    if (!kmsg) {
        return -ENOMEM;
    }
    memcpy(kmsg, &umsg, sizeof(*kmsg));

    kmsg->caps.count = 0;

    if (copy_buffer && umsg.buffer.data && umsg.buffer.size) {
        void *kbuf = kmalloc(umsg.buffer.size);
        if (!kbuf) {
            kfree(kmsg);
            return -ENOMEM;
        }
        ret = copy_from_user(kbuf, umsg.buffer.data, umsg.buffer.size);
        if (ret < 0) {
            kfree(kbuf);
            kfree(kmsg);
            return ret;
        }
        kmsg->buffer.data = kbuf;
        kmsg->buffer.size = umsg.buffer.size;
    } else {
        kmsg->buffer.data = NULL;
        kmsg->buffer.size = 0;
    }

    *out_kmsg = kmsg;
    return 0;
}

static int syscall_ipc_msg_copy_out(struct ipc_message *user_msg, const struct ipc_message *kmsg,
                                    void *user_buf_ptr, size_t user_buf_size) {
    if (!user_msg || !kmsg) {
        return -EINVAL;
    }

    if (user_buf_ptr && user_buf_size) {
        size_t n = kmsg->buffer.size;
        if (n > user_buf_size) {
            n = user_buf_size;
        }
        if (n) {
            int ret = copy_to_user(user_buf_ptr, kmsg->buffer.data, n);
            if (ret < 0) {
                return ret;
            }
        }
    }

    struct ipc_message out;
    memset(&out, 0, sizeof(out));
    memcpy(&out.regs, &kmsg->regs, sizeof(out.regs));
    out.buffer.data = user_buf_ptr;
    out.buffer.size = kmsg->buffer.size;
    out.caps.count  = 0;
    out.flags       = kmsg->flags;

    return copy_to_user(user_msg, &out, sizeof(out));
}

static void syscall_ipc_msg_free(struct ipc_message *kmsg) {
    if (!kmsg) {
        return;
    }
    if (kmsg->buffer.data) {
        kfree(kmsg->buffer.data);
    }
    kfree(kmsg);
}

void syscall_handler(struct irq_regs *regs) {
    uint32_t syscall_num = regs->eax;
    int      ret         = -ENOSYS;
    switch (syscall_num) {
    case SYS_EXIT:
        /* sys_exit(int code) */
        thread_exit((int)regs->ebx);
        break;

    case SYS_PUTC:
        /* sys_putc(char c) */
        kputc((char)(regs->ebx & 0xFF));
        ret = 0;
        break;

    case SYS_ENDPOINT_CREATE: {
        cap_handle_t h = endpoint_create();
        if (h == CAP_HANDLE_INVALID) {
            ret = -ENOMEM;
        } else {
            ret = (int)h;
        }
        break;
    }

    case SYS_IPC_SEND: {
        cap_handle_t        ep       = (cap_handle_t)regs->ebx;
        struct ipc_message *user_msg = (struct ipc_message *)regs->ecx;
        uint32_t            timeout  = regs->edx;
        struct ipc_message *kmsg     = NULL;
        ret                          = syscall_ipc_msg_copy_in(&kmsg, user_msg, true);
        if (ret < 0) {
            break;
        }
        ret = ipc_send(ep, kmsg, timeout);
        syscall_ipc_msg_free(kmsg);
        break;
    }

    case SYS_IPC_RECV: {
        cap_handle_t        ep       = (cap_handle_t)regs->ebx;
        struct ipc_message *user_msg = (struct ipc_message *)regs->ecx;
        uint32_t            timeout  = regs->edx;

        struct ipc_message umsg;
        ret = copy_from_user(&umsg, user_msg, sizeof(umsg));
        if (ret < 0) {
            break;
        }

        void  *user_buf_ptr  = umsg.buffer.data;
        size_t user_buf_size = umsg.buffer.size;

        struct ipc_message *kmsg = kzalloc(sizeof(*kmsg));
        if (!kmsg) {
            ret = -ENOMEM;
            break;
        }

        memcpy(&kmsg->regs, &umsg.regs, sizeof(kmsg->regs));
        kmsg->flags      = umsg.flags;
        kmsg->caps.count = 0;

        if (user_buf_ptr && user_buf_size) {
            kmsg->buffer.data = kmalloc(user_buf_size);
            if (!kmsg->buffer.data) {
                kfree(kmsg);
                ret = -ENOMEM;
                break;
            }
            kmsg->buffer.size = user_buf_size;
        } else {
            kmsg->buffer.data = NULL;
            kmsg->buffer.size = 0;
        }

        ret = ipc_receive(ep, kmsg, timeout);
        if (ret == 0) {
            ret = syscall_ipc_msg_copy_out(user_msg, kmsg, user_buf_ptr, user_buf_size);
        }
        syscall_ipc_msg_free(kmsg);
        break;
    }

    case SYS_IPC_CALL: {
        cap_handle_t        ep         = (cap_handle_t)regs->ebx;
        struct ipc_message *user_req   = (struct ipc_message *)regs->ecx;
        struct ipc_message *user_reply = (struct ipc_message *)regs->edx;
        uint32_t            timeout    = regs->esi;

        struct ipc_message *kreq = NULL;
        ret                      = syscall_ipc_msg_copy_in(&kreq, user_req, true);
        if (ret < 0) {
            break;
        }

        struct ipc_message ureply;
        ret = copy_from_user(&ureply, user_reply, sizeof(ureply));
        if (ret < 0) {
            syscall_ipc_msg_free(kreq);
            break;
        }

        void  *user_buf_ptr  = ureply.buffer.data;
        size_t user_buf_size = ureply.buffer.size;

        struct ipc_message *kreply = kzalloc(sizeof(*kreply));
        if (!kreply) {
            syscall_ipc_msg_free(kreq);
            ret = -ENOMEM;
            break;
        }

        memcpy(&kreply->regs, &ureply.regs, sizeof(kreply->regs));
        kreply->flags      = ureply.flags;
        kreply->caps.count = 0;

        if (user_buf_ptr && user_buf_size) {
            kreply->buffer.data = kmalloc(user_buf_size);
            if (!kreply->buffer.data) {
                syscall_ipc_msg_free(kreq);
                kfree(kreply);
                ret = -ENOMEM;
                break;
            }
            kreply->buffer.size = user_buf_size;
        } else {
            kreply->buffer.data = NULL;
            kreply->buffer.size = 0;
        }

        ret = ipc_call(ep, kreq, kreply, timeout);
        if (ret == 0) {
            ret = syscall_ipc_msg_copy_out(user_reply, kreply, user_buf_ptr, user_buf_size);
        }

        syscall_ipc_msg_free(kreq);
        syscall_ipc_msg_free(kreply);
        break;
    }

    case SYS_IPC_REPLY: {
        struct ipc_message *user_reply = (struct ipc_message *)regs->ebx;
        struct ipc_message *kreply     = NULL;
        ret                            = syscall_ipc_msg_copy_in(&kreply, user_reply, true);
        if (ret < 0) {
            break;
        }
        ret = ipc_reply(kreply);
        syscall_ipc_msg_free(kreply);
        break;
    }

    case SYS_IOPORT_OUTB: {
        cap_handle_t         h    = (cap_handle_t)regs->ebx;
        uint16_t             port = (uint16_t)regs->ecx;
        uint8_t              val  = (uint8_t)regs->edx;
        struct process      *proc = (struct process *)process_current();
        struct ioport_range *r    = cap_lookup(proc, h, CAP_TYPE_IOPORT, CAP_WRITE);
        if (!r || !ioport_range_contains(r, port)) {
            ret = -EPERM;
            break;
        }
        outb(port, val);
        ret = 0;
        break;
    }

    case SYS_IOPORT_INB: {
        cap_handle_t         h    = (cap_handle_t)regs->ebx;
        uint16_t             port = (uint16_t)regs->ecx;
        struct process      *proc = (struct process *)process_current();
        struct ioport_range *r    = cap_lookup(proc, h, CAP_TYPE_IOPORT, CAP_READ);
        if (!r || !ioport_range_contains(r, port)) {
            ret = -EPERM;
            break;
        }
        ret = (int)inb(port);
        break;
    }

    default:
        pr_warn("Unknown syscall: %d", syscall_num);
        ret = -1;
        break;
    }

    regs->eax = ret;
}
