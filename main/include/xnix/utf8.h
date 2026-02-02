#ifndef XNIX_UTF8_H
#define XNIX_UTF8_H

#include <xnix/types.h>

/**
 * UTF-8 流式解码
 *
 * @param state     解码状态,初始为 0
 * @param codepoint 输出的 Unicode 码点
 * @param byte      输入字节
 * @return >0: 完整码点已解码到 codepoint
 *          0: 需要更多字节
 *         <0: 解码错误,state 已重置
 */
int utf8_decode_byte(uint32_t *state, uint32_t *codepoint, uint8_t byte);

/**
 * 一次性解码(从字符串)
 *
 * @param s 指向字符串指针的指针,会被移动到下一个码点
 * @return Unicode 码点,或 0 表示字符串结束
 */
uint32_t utf8_decode(const char **s);

/**
 * 获取 UTF-8 字符的字节长度
 *
 * @param lead_byte UTF-8 序列的首字节
 * @return 字节长度 (1-4),或 0 表示无效
 */
int utf8_char_length(uint8_t lead_byte);

#endif
