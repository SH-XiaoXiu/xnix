/**
 * @file svc.h
 * @brief 服务管理接口
 *
 * 提供服务生命周期通知和服务连接 API.
 * 应用通过 svc_connect/svc_send/svc_recv 与服务交互,
 * 不需要直接操纵 handle.
 */

#ifndef XNIX_SVC_H
#define XNIX_SVC_H

#include <stddef.h>
#include <stdint.h>

/**
 * 通知 init 服务已就绪
 *
 * @param name 服务名称
 * @return 0 成功,负数失败
 */
int svc_notify_ready(const char *name);

/**
 * 连接到已有服务(按名称查找 endpoint)
 *
 * 查找名称对应的 IPC endpoint handle,包装为 fd.
 *
 * @param service_name endpoint 名称(如 "vfs_ep", "sudo_ep")
 * @return fd >= 0 成功, -1 失败(设置 errno)
 */
int svc_connect(const char *service_name);

/**
 * 创建服务 endpoint 并返回 fd
 *
 * 创建新的 IPC endpoint,包装为 fd.
 *
 * @param service_name endpoint 名称
 * @return fd >= 0 成功, -1 失败(设置 errno)
 */
int svc_create(const char *service_name);

/**
 * 通过 fd 发送 IPC 消息
 *
 * @param fd   IPC 文件描述符
 * @param msg  消息缓冲区
 * @param len  消息长度
 * @return 0 成功, -1 失败(设置 errno)
 */
int svc_send(int fd, const void *msg, size_t len);

/**
 * 通过 fd 接收 IPC 消息
 *
 * @param fd   IPC 文件描述符
 * @param msg  接收缓冲区
 * @param len  缓冲区大小
 * @return 实际接收字节数, -1 失败(设置 errno)
 */
int svc_recv(int fd, void *msg, size_t len);

#endif /* XNIX_SVC_H */
