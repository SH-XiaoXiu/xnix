/**
 * @file scancode.c
 * @brief Scancode Set 1 翻译
 */

#include "scancode.h"

#include <stdbool.h>

/* 修饰键状态 */
static bool shift_held = false;
static bool ctrl_held  = false;
static bool caps_lock  = false;

/* Scancode Set 1 映射表(无修饰) */
static const char scancode_normal[128] = {
    0,    0x1B, '1', '2',  '3',  '4', '5',  '6',  /* 0x00-0x07 */
    '7',  '8',  '9', '0',  '-',  '=', '\b', '\t', /* 0x08-0x0F */
    'q',  'w',  'e', 'r',  't',  'y', 'u',  'i',  /* 0x10-0x17 */
    'o',  'p',  '[', ']',  '\n', 0,   'a',  's',  /* 0x18-0x1F */
    'd',  'f',  'g', 'h',  'j',  'k', 'l',  ';',  /* 0x20-0x27 */
    '\'', '`',  0,   '\\', 'z',  'x', 'c',  'v',  /* 0x28-0x2F */
    'b',  'n',  'm', ',',  '.',  '/', 0,    '*',  /* 0x30-0x37 */
    0,    ' ',  0,   0,    0,    0,   0,    0,    /* 0x38-0x3F */
    0,    0,    0,   0,    0,    0,   0,    '7',  /* 0x40-0x47 */
    '8',  '9',  '-', '4',  '5',  '6', '+',  '1',  /* 0x48-0x4F */
    '2',  '3',  '0', '.',  0,    0,   0,    0,    /* 0x50-0x57 */
};

/* Scancode Set 1 映射表(Shift) */
static const char scancode_shift[128] = {
    0,   0x1B, '!', '@', '#',  '$', '%',  '^',  /* 0x00-0x07 */
    '&', '*',  '(', ')', '_',  '+', '\b', '\t', /* 0x08-0x0F */
    'Q', 'W',  'E', 'R', 'T',  'Y', 'U',  'I',  /* 0x10-0x17 */
    'O', 'P',  '{', '}', '\n', 0,   'A',  'S',  /* 0x18-0x1F */
    'D', 'F',  'G', 'H', 'J',  'K', 'L',  ':',  /* 0x20-0x27 */
    '"', '~',  0,   '|', 'Z',  'X', 'C',  'V',  /* 0x28-0x2F */
    'B', 'N',  'M', '<', '>',  '?', 0,    '*',  /* 0x30-0x37 */
    0,   ' ',  0,   0,   0,    0,   0,    0,    /* 0x38-0x3F */
};

/* 特殊扫描码 */
#define SC_LSHIFT_PRESS   0x2A
#define SC_RSHIFT_PRESS   0x36
#define SC_LSHIFT_RELEASE 0xAA
#define SC_RSHIFT_RELEASE 0xB6
#define SC_LCTRL_PRESS    0x1D
#define SC_LCTRL_RELEASE  0x9D
#define SC_CAPS_PRESS     0x3A

int scancode_to_char(uint8_t scancode) {
    /* 检查是否为释放码(最高位为 1) */
    bool    release = (scancode & 0x80) != 0;
    uint8_t code    = scancode & 0x7F;

    /* 处理修饰键 */
    switch (scancode) {
    case SC_LSHIFT_PRESS:
    case SC_RSHIFT_PRESS:
        shift_held = true;
        return -1;
    case SC_LSHIFT_RELEASE:
    case SC_RSHIFT_RELEASE:
        shift_held = false;
        return -1;
    case SC_LCTRL_PRESS:
        ctrl_held = true;
        return -1;
    case SC_LCTRL_RELEASE:
        ctrl_held = false;
        return -1;
    case SC_CAPS_PRESS:
        caps_lock = !caps_lock;
        return -1;
    }

    /* 释放码不产生字符 */
    if (release) {
        return -1;
    }

    /* 查表 */
    char c;
    if (shift_held) {
        c = scancode_shift[code];
    } else {
        c = scancode_normal[code];
    }

    /* Caps Lock 只影响字母 */
    if (caps_lock && c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    } else if (caps_lock && c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
    }

    /* Ctrl 组合键 */
    if (ctrl_held && c >= 'a' && c <= 'z') {
        c = c - 'a' + 1; /* Ctrl+A = 1, Ctrl+C = 3, etc. */
    }

    return c ? c : -1;
}
