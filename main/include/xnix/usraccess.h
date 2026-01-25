#ifndef XNIX_UACCESS_H
#define XNIX_UACCESS_H

#include <xnix/types.h>

/**
 * 从用户地址空间复制到内核缓冲区
 *
 * @param dst      内核目标缓冲区
 * @param user_src 用户源地址
 * @param n        拷贝字节数
 * @return 0 成功, <0 失败(-EFAULT/-EINVAL/-ENOSYS)
 */
int copy_from_user(void *dst, const void *user_src, size_t n);

/**
 * 从内核缓冲区复制到用户地址空间
 *
 * @param user_dst 用户目标地址
 * @param src      内核源缓冲区
 * @param n        拷贝字节数
 * @return 0 成功, <0 失败(-EFAULT/-EINVAL/-ENOSYS)
 */
int copy_to_user(void *user_dst, const void *src, size_t n);

#endif /* XNIX_UACCESS_H */
