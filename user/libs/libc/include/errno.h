/**
 * @file errno.h
 * @brief POSIX 标准 errno 支持
 *
 * 提供标准的 errno 全局变量,strerror 和 perror 函数.
 * 每个线程有独立的 errno 值(使用 __thread 声明).
 */

#ifndef _ERRNO_H
#define _ERRNO_H

#include <xnix/abi/errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 全局错误码
 * 系统调用失败时设置,成功时不修改
 * TODO: 实现 TLS 支持后改为线程局部变量
 */
extern int errno;

/**
 * 将错误码转换为错误消息字符串
 * @param errnum 错误码
 * @return 错误消息字符串(不可修改)
 */
const char *strerror(int errnum);

/**
 * 打印错误消息到标准错误输出
 * 格式:<s>: <strerror(errno)>
 * @param s 用户提供的前缀字符串,可为 NULL
 */
void perror(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* _ERRNO_H */
