/**
 * @file kerr.h
 * @brief 内核错误码可读字符串
 */

#ifndef XNIX_KERR_H
#define XNIX_KERR_H

/**
 * 将错误码转换为可读字符串
 *
 * 接受正数或负数,返回错误描述.
 * 未知错误码返回 "unknown error".
 *
 * @param errnum 错误码(正数或负数均可)
 * @return 静态字符串,不可释放
 */
const char *kerr(int errnum);

#endif /* XNIX_KERR_H */
