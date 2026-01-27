/**
 * @file abi/errno.h
 * @brief 系统错误码 ABI 定义
 */

#ifndef XNIX_ABI_ERRNO_H
#define XNIX_ABI_ERRNO_H

/*
 * 通用错误码
 *
 * 约定:负数表示错误,0 表示成功
 */
#define ABI_EOK      0   /* 成功 */
#define ABI_EPERM    1   /* 权限不足 */
#define ABI_ENOENT   2   /* 不存在 */
#define ABI_EINTR    3   /* 被中断 */
#define ABI_EIO      4   /* I/O 错误 */
#define ABI_ENOMEM   5   /* 内存不足 */
#define ABI_EACCES   6   /* 访问被拒绝 */
#define ABI_EFAULT   7   /* 地址错误 */
#define ABI_EBUSY    8   /* 资源忙 */
#define ABI_EEXIST   9   /* 已存在 */
#define ABI_EINVAL   10  /* 无效参数 */
#define ABI_ENOSPC   11  /* 空间不足 */
#define ABI_EAGAIN   12  /* 重试 */
#define ABI_ETIMEDOUT 13 /* 超时 */
#define ABI_ENOSYS   14  /* 未实现 */
#define ABI_ERANGE   15  /* 超出范围 */

#endif /* XNIX_ABI_ERRNO_H */
