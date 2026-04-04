/**
 * @file io.h
 * @brief 统一 IO 协议 ABI 定义
 *
 * 所有支持 fd 操作的 server 必须实现这些 opcode.
 * libc 的 read/write/close/ioctl 统一发送这些消息,
 * 不关心对端是 TTY / VFS / syslog 还是其他服务.
 *
 * 消息格式约定:
 *   请求: data[0]=opcode, data[1]=session, data[2]=offset, data[3]=size/cmd
 *   回复: data[0]=result (>=0 成功, <0 errno)
 *
 * opcode 从 0x100 开始, 不与任何 server 私有 opcode 冲突.
 */

#ifndef XNIX_ABI_IO_H
#define XNIX_ABI_IO_H

/*
 * IO_READ
 *   请求: data[0]=IO_READ, data[1]=session, data[2]=offset, data[3]=max_size
 *          reply.buffer = 接收缓冲区
 *   回复: data[0]=bytes_read (0=EOF, <0=errno), buffer=数据
 */
#define IO_READ  0x100

/*
 * IO_WRITE
 *   请求: data[0]=IO_WRITE, data[1]=session, data[2]=offset, data[3]=size
 *          buffer = 写入数据
 *   回复: data[0]=bytes_written (<0=errno)
 */
#define IO_WRITE 0x101

/*
 * IO_CLOSE
 *   请求: data[0]=IO_CLOSE, data[1]=session
 *   回复: data[0]=0 或负 errno
 */
#define IO_CLOSE 0x102

/*
 * IO_IOCTL
 *   请求: data[0]=IO_IOCTL, data[1]=session, data[2]=cmd, data[3..]=参数
 *          buffer = 可选数据
 *   回复: data[0]=result 或负 errno
 */
#define IO_IOCTL 0x103

#endif /* XNIX_ABI_IO_H */
