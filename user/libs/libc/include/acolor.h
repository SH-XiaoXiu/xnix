#ifndef _ACOLOR_H
#define _ACOLOR_H

/* 重置所有属性 */
#define ACOLOR_RESET        "\x1b[0m"

/* 普通前景色 */
#define ACOLOR_BLACK        "\x1b[30m"  /* 黑色 */
#define ACOLOR_RED          "\x1b[31m"  /* 红色 */
#define ACOLOR_GREEN        "\x1b[32m"  /* 绿色 */
#define ACOLOR_YELLOW       "\x1b[33m"  /* 黄色 */
#define ACOLOR_BLUE         "\x1b[34m"  /* 蓝色 */
#define ACOLOR_MAGENTA      "\x1b[35m"  /* 品红 */
#define ACOLOR_CYAN         "\x1b[36m"  /* 青色 */
#define ACOLOR_WHITE        "\x1b[37m"  /* 白色 */

/* 高亮(亮色)前景色 */
#define ACOLOR_BBLACK       "\x1b[90m"  /* 亮黑(灰) */
#define ACOLOR_BRED         "\x1b[91m"  /* 亮红 */
#define ACOLOR_BGREEN       "\x1b[92m"  /* 亮绿 */
#define ACOLOR_BYELLOW      "\x1b[93m"  /* 亮黄 */
#define ACOLOR_BBLUE        "\x1b[94m"  /* 亮蓝 */
#define ACOLOR_BMAGENTA     "\x1b[95m"  /* 亮品红 */
#define ACOLOR_BCYAN        "\x1b[96m"  /* 亮青 */
#define ACOLOR_BWHITE       "\x1b[97m"  /* 亮白 */

/* 普通背景色 */
#define ACOLOR_BG_BLACK     "\x1b[40m"  /* 黑色背景 */
#define ACOLOR_BG_RED       "\x1b[41m"  /* 红色背景 */
#define ACOLOR_BG_GREEN     "\x1b[42m"  /* 绿色背景 */
#define ACOLOR_BG_YELLOW    "\x1b[43m"  /* 黄色背景 */
#define ACOLOR_BG_BLUE      "\x1b[44m"  /* 蓝色背景 */
#define ACOLOR_BG_MAGENTA   "\x1b[45m"  /* 品红背景 */
#define ACOLOR_BG_CYAN      "\x1b[46m"  /* 青色背景 */
#define ACOLOR_BG_WHITE     "\x1b[47m"  /* 白色背景 */

/* 高亮(亮色)背景色 */
#define ACOLOR_BG_BBLACK    "\x1b[100m" /* 亮黑背景 */
#define ACOLOR_BG_BRED      "\x1b[101m" /* 亮红背景 */
#define ACOLOR_BG_BGREEN    "\x1b[102m" /* 亮绿背景 */
#define ACOLOR_BG_BYELLOW   "\x1b[103m" /* 亮黄背景 */
#define ACOLOR_BG_BBLUE     "\x1b[104m" /* 亮蓝背景 */
#define ACOLOR_BG_BMAGENTA  "\x1b[105m" /* 亮品红背景 */
#define ACOLOR_BG_BCYAN     "\x1b[106m" /* 亮青背景 */
#define ACOLOR_BG_BWHITE    "\x1b[107m" /* 亮白背景 */

/* 文本样式 */
#define ACOLOR_BOLD         "\x1b[1m"   /* 加粗 */
#define ACOLOR_DIM          "\x1b[2m"   /* 变暗 */
#define ACOLOR_ITALIC       "\x1b[3m"   /* 斜体 */
#define ACOLOR_UNDERLINE    "\x1b[4m"   /* 下划线 */
#define ACOLOR_BLINK        "\x1b[5m"   /* 闪烁 */
#define ACOLOR_REVERSE      "\x1b[7m"   /* 前景背景反转 */
#define _ACOLOR_HIDDEN       "\x1b[8m"   /* 隐藏文字 */
#define ACOLOR_STRIKE       "\x1b[9m"   /* 删除线 */

#define ACOLOR_WRAP(color, text) color text ACOLOR_RESET

/* 常用快捷宏 */
#define ACOLOR_RED_T(text)     ACOLOR_WRAP(ACOLOR_RED, text)
#define ACOLOR_GREEN_T(text)   ACOLOR_WRAP(ACOLOR_GREEN, text)
#define ACOLOR_YELLOW_T(text)  ACOLOR_WRAP(ACOLOR_YELLOW, text)
#define ACOLOR_BLUE_T(text)    ACOLOR_WRAP(ACOLOR_BLUE, text)

#endif /* _ACOLOR_H */
