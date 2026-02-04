/**
 * @file svc.h
 * @brief 服务管理接口
 */

#ifndef XNIX_SVC_H
#define XNIX_SVC_H

/**
 * 通知 init 服务已就绪
 *
 * @param name 服务名称
 * @return 0 成功,负数失败
 */
int svc_notify_ready(const char *name);

#endif /* XNIX_SVC_H */
