/**
 * @file abi/tty.h
 * @brief TTY ABI 常量
 */

#ifndef XNIX_ABI_TTY_H
#define XNIX_ABI_TTY_H

/* TTY Handle 名称约定 */
#define ABI_TTY0_HANDLE_NAME "tty0" /* VGA 终端 */
#define ABI_TTY1_HANDLE_NAME "tty1" /* Serial 终端 */

/* TTY 数量上限 */
#define ABI_TTY_MAX 4

#endif /* XNIX_ABI_TTY_H */
