/**
 * @file input.h
 * @brief 内核输入队列接口
 */

#ifndef KERNEL_IO_INPUT_H
#define KERNEL_IO_INPUT_H

/**
 * 初始化输入子系统
 */
void input_init(void);

/**
 * 写入一个字符到输入队列
 *
 * @param c 字符
 * @return 0 成功,-1 缓冲区满
 */
int input_write(char c);

/**
 * 从输入队列读取一个字符(阻塞)
 *
 * @return 字符
 */
int input_read(void);

#endif /* KERNEL_IO_INPUT_H */
