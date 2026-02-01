/**
 * @file path.h
 * @brief Shell PATH 管理
 */

#ifndef SHELL_PATH_H
#define SHELL_PATH_H

#include <stdbool.h>
#include <stddef.h>

#define SHELL_MAX_PATHS 8
#define SHELL_PATH_LEN  128

/**
 * 初始化 PATH(设置默认路径)
 */
void path_init(void);

/**
 * 添加搜索路径
 * @param dir 目录路径
 * @return true 成功,false 失败(路径满)
 */
bool path_add(const char *dir);

/**
 * 清空所有路径
 */
void path_clear(void);

/**
 * 获取路径数量
 */
int path_count(void);

/**
 * 获取指定索引的路径
 * @param index 路径索引
 * @return 路径字符串,无效索引返回 NULL
 */
const char *path_get(int index);

/**
 * 查找可执行文件
 * @param name    命令名
 * @param out     输出完整路径
 * @param max_len 输出缓冲区大小
 * @return true 找到,false 未找到
 */
bool path_find(const char *name, char *out, size_t max_len);

#endif /* SHELL_PATH_H */
