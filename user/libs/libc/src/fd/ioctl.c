/**
 * @file ioctl.c
 * @brief 统一 fd ioctl 实现
 */

#include <stdarg.h>
#include <sys/ioctl.h>
#include <xnix/abi/io.h>
#include <xnix/protocol/tty.h>
#include <errno.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

int ioctl(int fd, unsigned long cmd, ...) {
    struct fd_entry *ent = fd_get(fd);
    if (!ent) {
        errno = EBADF;
        return -1;
    }

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_IOCTL;
    msg.regs.data[1] = ent->session;
    msg.regs.data[2] = (uint32_t)cmd;

    va_list ap;
    va_start(ap, cmd);
    switch (cmd) {
    case TTY_IOCTL_SET_FOREGROUND:
    case TTY_IOCTL_SET_ECHO:
    case TTY_IOCTL_SET_ACTIVE_TTY:
        msg.regs.data[3] = va_arg(ap, unsigned int);
        break;
    case TTY_IOCTL_SET_COLOR:
        msg.regs.data[3] = va_arg(ap, unsigned int);
        msg.regs.data[4] = va_arg(ap, unsigned int);
        break;
    default:
        break;
    }
    va_end(ap);

    int ret = sys_ipc_call(ent->handle, &msg, &reply, 1000);
    if (ret < 0) {
        errno = EIO;
        return -1;
    }

    int32_t result = (int32_t)reply.regs.data[0];
    if (result < 0) {
        errno = -result;
        return -1;
    }

    return result;
}
