/**
 * @file abi/input.h
 * @brief 统一输入事件 ABI 定义
 *
 * 为 GUI 和用户态提供结构化输入事件.
 * 键盘和鼠标驱动都使用此格式输出事件.
 */

#ifndef XNIX_ABI_INPUT_H
#define XNIX_ABI_INPUT_H

#include <xnix/abi/types.h>

/**
 * 输入事件类型
 */
enum input_event_type {
    INPUT_EVENT_KEY_PRESS    = 1, /* 按键按下 */
    INPUT_EVENT_KEY_RELEASE  = 2, /* 按键释放 */
    INPUT_EVENT_MOUSE_MOVE   = 3, /* 鼠标移动 */
    INPUT_EVENT_MOUSE_BUTTON = 4, /* 鼠标按键变化 */
};

/**
 * 修饰键位掩码
 */
#define INPUT_MOD_SHIFT    (1 << 0)
#define INPUT_MOD_CTRL     (1 << 1)
#define INPUT_MOD_ALT      (1 << 2)
#define INPUT_MOD_CAPSLOCK (1 << 3)

/**
 * 统一输入事件 (16 字节,适合 4 个 IPC 寄存器)
 */
struct input_event {
    uint8_t  type;      /* input_event_type */
    uint8_t  modifiers; /* INPUT_MOD_* 位掩码 */
    uint16_t code;      /* 按键: scancode(& 0x7F), 鼠标按键: 按键索引 */
    int16_t  value;     /* 按键: 1=press/0=release, 鼠标移动: dx, 按键: 1/0 */
    int16_t  value2;    /* 鼠标移动: dy, 其他: 0 */
    uint32_t timestamp; /* 毫秒级时间戳(系统启动后) */
    uint32_t _reserved;
};

/*
 * 扩展按键码 (E0 扫描码映射到 0x80+ 范围)
 */
#define INPUT_KEY_UP    0x80
#define INPUT_KEY_DOWN  0x81
#define INPUT_KEY_LEFT  0x82
#define INPUT_KEY_RIGHT 0x83

/*
 * 鼠标按键码 (用于 INPUT_EVENT_MOUSE_BUTTON 的 code 字段)
 */
#define INPUT_MOUSE_BTN_LEFT   0
#define INPUT_MOUSE_BTN_RIGHT  1
#define INPUT_MOUSE_BTN_MIDDLE 2

#endif /* XNIX_ABI_INPUT_H */
