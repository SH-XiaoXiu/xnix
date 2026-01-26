#ifndef XNIX_CONSOLE_UDM_H
#define XNIX_CONSOLE_UDM_H

/**
 * 控制台 UDM/IPC 协议
 *
 * 约定:
 * - msg.regs.data[0] 为 opcode
 * - 其余参数放在 data[1..] 中
 * - 当前只覆盖最小控制台功能(putc/颜色/清屏)
 */
#define CONSOLE_UDM_OPS(X) \
    X(PUTC, 1)             \
    X(SET_COLOR, 2)        \
    X(RESET_COLOR, 3)      \
    X(CLEAR, 4)

enum console_udm_op {
#define CONSOLE_UDM_OP_ENUM(name, val) CONSOLE_UDM_OP_##name = (val),
    CONSOLE_UDM_OPS(CONSOLE_UDM_OP_ENUM)
#undef CONSOLE_UDM_OP_ENUM
};

#endif
