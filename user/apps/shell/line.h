/**
 * @file line.h
 * @brief Shell 行编辑器 (Tab 补全 + 命令历史)
 */

#ifndef SHELL_LINE_H
#define SHELL_LINE_H

#include <stdbool.h>

#define LINE_MAX_LEN   256
#define HISTORY_MAX    64
#define HISTORY_FILE_DEFAULT "/etc/shell_history"

/* 运行时 history 路径 (可被 shell main 覆盖为 ~/. shell_history) */
extern char g_history_file[128];

/**
 * 初始化行编辑器, 加载历史文件
 */
void line_init(void);

/**
 * 读取一行输入 (RAW 模式, 支持 Tab 补全和箭头历史)
 *
 * @param buf     输出缓冲区
 * @param size    缓冲区大小
 * @param prompt  提示符字符串
 * @return buf 成功, NULL 失败/EOF
 */
char *line_read(char *buf, int size, const char *prompt);

/**
 * 添加命令到历史
 */
void line_add_history(const char *cmd);

/**
 * 保存历史到文件
 */
void line_save_history(void);

#endif /* SHELL_LINE_H */
