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
static bool e0_prefix  = false;

/* Scancode Set 1 映射表(无修饰) */
static const char scancode_normal[128] = {
    0,    0x1B, '1', '2',  '3',  '4', '5',  '6',
    '7',  '8',  '9', '0',  '-',  '=', '\b', '\t',
    'q',  'w',  'e', 'r',  't',  'y', 'u',  'i',
    'o',  'p',  '[', ']',  '\n', 0,   'a',  's',
    'd',  'f',  'g', 'h',  'j',  'k', 'l',  ';',
    '\'', '`',  0,   '\\', 'z',  'x', 'c',  'v',
    'b',  'n',  'm', ',',  '.',  '/', 0,    '*',
    0,    ' ',  0,   0,    0,    0,   0,    0,
    0,    0,    0,   0,    0,    0,   0,    '7',
    '8',  '9',  '-', '4',  '5',  '6', '+',  '1',
    '2',  '3',  '0', '.',  0,    0,   0,    0,
};

/* Scancode Set 1 映射表(Shift) */
static const char scancode_shift[128] = {
    0,   0x1B, '!', '@', '#',  '$', '%',  '^',
    '&', '*',  '(', ')', '_',  '+', '\b', '\t',
    'Q', 'W',  'E', 'R', 'T',  'Y', 'U',  'I',
    'O', 'P',  '{', '}', '\n', 0,   'A',  'S',
    'D', 'F',  'G', 'H', 'J',  'K', 'L',  ':',
    '"', '~',  0,   '|', 'Z',  'X', 'C',  'V',
    'B', 'N',  'M', '<', '>',  '?', 0,    '*',
    0,   ' ',  0,   0,   0,    0,   0,    0,
};

#define SC_LSHIFT_PRESS   0x2A
#define SC_RSHIFT_PRESS   0x36
#define SC_LSHIFT_RELEASE 0xAA
#define SC_RSHIFT_RELEASE 0xB6
#define SC_LCTRL_PRESS    0x1D
#define SC_LCTRL_RELEASE  0x9D
#define SC_CAPS_PRESS     0x3A
#define SC_E0_PREFIX      0xE0

#define SC_E0_UP    0x48
#define SC_E0_DOWN  0x50
#define SC_E0_LEFT  0x4B
#define SC_E0_RIGHT 0x4D

static bool e0_prefix_ev = false;

static int translate_printable_code(uint8_t code) {
    char c = shift_held ? scancode_shift[code] : scancode_normal[code];

    if (caps_lock && c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    } else if (caps_lock && c >= 'A' && c <= 'Z') {
        c = (char)(c - 'A' + 'a');
    }

    if (ctrl_held && c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 1);
    }

    return c ? (unsigned char)c : -1;
}

int scancode_to_char(uint8_t scancode) {
    if (scancode == SC_E0_PREFIX) {
        e0_prefix = true;
        return -1;
    }

    bool    release = (scancode & 0x80) != 0;
    uint8_t code    = scancode & 0x7F;

    if (e0_prefix) {
        e0_prefix = false;
        if (release) {
            return -1;
        }
        switch (code) {
        case SC_E0_UP:
            return KEY_UP;
        case SC_E0_DOWN:
            return KEY_DOWN;
        case SC_E0_LEFT:
            return KEY_LEFT;
        case SC_E0_RIGHT:
            return KEY_RIGHT;
        }
        return -1;
    }

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
    default:
        break;
    }

    if (release) {
        return -1;
    }

    return translate_printable_code(code);
}

uint8_t scancode_get_modifiers(void) {
    uint8_t mods = 0;
    if (shift_held) mods |= INPUT_MOD_SHIFT;
    if (ctrl_held)  mods |= INPUT_MOD_CTRL;
    if (caps_lock)  mods |= INPUT_MOD_CAPSLOCK;
    return mods;
}

int scancode_to_event(uint8_t scancode, struct input_event *ev) {
    if (scancode == SC_E0_PREFIX) {
        e0_prefix_ev = true;
        return -1;
    }

    bool    release = (scancode & 0x80) != 0;
    uint8_t code    = scancode & 0x7F;

    if (e0_prefix_ev) {
        e0_prefix_ev = false;
        ev->type      = release ? INPUT_EVENT_KEY_RELEASE : INPUT_EVENT_KEY_PRESS;
        ev->modifiers = scancode_get_modifiers();
        ev->value     = release ? 0 : 1;
        ev->value2    = 0;
        ev->timestamp = 0;
        ev->_reserved = 0;
        switch (code) {
        case SC_E0_UP:    ev->code = INPUT_KEY_UP;    break;
        case SC_E0_DOWN:  ev->code = INPUT_KEY_DOWN;  break;
        case SC_E0_LEFT:  ev->code = INPUT_KEY_LEFT;  break;
        case SC_E0_RIGHT: ev->code = INPUT_KEY_RIGHT; break;
        default:          ev->code = 0x80 | code;     break;
        }
        return 0;
    }

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
    default:
        break;
    }

    ev->type      = release ? INPUT_EVENT_KEY_RELEASE : INPUT_EVENT_KEY_PRESS;
    ev->modifiers = scancode_get_modifiers();
    ev->value     = release ? 0 : 1;
    ev->value2    = 0;
    ev->timestamp = 0;
    ev->_reserved = 0;

    {
        int printable = translate_printable_code(code);
        ev->code = (printable >= 0) ? (uint16_t)printable : code;
    }
    return 0;
}
