/**
 * @file ini_parser.h
 * @brief 简单的 INI 文件解析器
 */

#ifndef INI_PARSER_H
#define INI_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#define INI_MAX_LINE    256
#define INI_MAX_SECTION 64
#define INI_MAX_KEY     32
#define INI_MAX_VALUE   192

/**
 * 解析回调函数
 *
 * @param section 当前 section 名称(如 "service.shell")
 * @param key     键名
 * @param value   值
 * @param ctx     用户上下文
 * @return true 继续解析,false 停止解析
 */
typedef bool (*ini_handler_t)(const char *section, const char *key, const char *value, void *ctx);

/**
 * 解析 INI 文件
 *
 * @param path    文件路径
 * @param handler 回调函数
 * @param ctx     用户上下文
 * @return 0 成功,负数失败
 */
int ini_parse_file(const char *path, ini_handler_t handler, void *ctx);

/**
 * 解析 INI 格式的字符串缓冲区
 *
 * @param buf     缓冲区
 * @param len     缓冲区长度
 * @param handler 回调函数
 * @param ctx     用户上下文
 * @return 0 成功,负数失败
 */
int ini_parse_buffer(const char *buf, size_t len, ini_handler_t handler, void *ctx);

#endif /* INI_PARSER_H */
