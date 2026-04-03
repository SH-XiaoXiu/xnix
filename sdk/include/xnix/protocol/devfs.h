/**
 * @file xnix/protocol/devfs.h
 * @brief Device Filesystem IPC Protocol
 *
 * 定义 devfsd 的设备注册协议和设备类型.
 * 块设备注册 (UDM_DEVFS_REGISTER_BLOCK) 仍在 blk.h 中定义.
 */

#ifndef XNIX_PROTOCOL_DEVFS_H
#define XNIX_PROTOCOL_DEVFS_H

#include <stdint.h>

/* 设备文件类型 (用于 devfs 内部和 VFS open reply data[2]) */
#define DEVFS_TYPE_FILE     0   /* 普通 VFS 文件 (默认) */
#define DEVFS_TYPE_TTY      1   /* TTY endpoint 设备 */

/**
 * DEVFS_REGISTER_TTY - 向 devfsd 注册 TTY 设备
 *
 * 由 ttyd 在就绪后发送给 devfsd.
 * 注册后可通过 open("/dev/ttyN") 获取 TTY endpoint handle.
 *
 * Request:
 *   regs[0] = UDM_DEVFS_REGISTER_TTY
 *   regs[1] = tty_index
 *   handles[0] = tty endpoint handle
 *   buffer  = 设备名 (e.g. "tty0", 不含 \0 终止)
 *
 * Reply:
 *   regs[1] = 0 (成功) 或错误码
 */
#define UDM_DEVFS_REGISTER_TTY 201

#endif /* XNIX_PROTOCOL_DEVFS_H */
