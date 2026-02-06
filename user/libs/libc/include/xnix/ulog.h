/**
 * @file ulog.h
 * @brief 用户态日志输出便捷接口
 */

#ifndef XNIX_ULOG_H
#define XNIX_ULOG_H

#include <stdarg.h>
#include <stdio.h>
#include <xnix/termcolor.h>

int ulog_tagf(FILE *stream, enum term_color tag_color, const char *tag, const char *fmt, ...);
int ulog_vtagf(FILE *stream, enum term_color tag_color, const char *tag, const char *fmt,
               va_list ap);

int ulog_okf(const char *fmt, ...);
int ulog_infof(const char *fmt, ...);
int ulog_warnf(const char *fmt, ...);
int ulog_errf(const char *fmt, ...);

#endif /* XNIX_ULOG_H */
