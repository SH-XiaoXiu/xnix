/**
 * @file ioctl.h
 * @brief 通用 fd ioctl 接口
 */

#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

int ioctl(int fd, unsigned long cmd, ...);

#endif /* _SYS_IOCTL_H */
