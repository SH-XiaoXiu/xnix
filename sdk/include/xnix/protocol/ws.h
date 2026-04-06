/**
 * @file xnix/protocol/ws.h
 * @brief 窗口服务器 IPC 协议定义
 *
 * 定义 wsd (Window Server Daemon) 与客户端之间的 IPC 操作码和消息格式.
 * 操作码从 0x200 开始, 不与 fb (1-6) / input (0x100-0x101) 协议冲突.
 */

#ifndef XNIX_PROTOCOL_WS_H
#define XNIX_PROTOCOL_WS_H

#include <stdint.h>
#include <xnix/abi/input.h>
#include <xnix/protocol/input.h>

/* 消息解析辅助宏 */
#ifndef UDM_MSG_OPCODE
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])
#endif

/**
 * 窗口服务器操作码
 */
enum ws_op {
    /* Client -> Server */
    WS_OP_CREATE_WINDOW   = 0x200, /* 创建窗口 */
    WS_OP_DESTROY_WINDOW  = 0x201, /* 销毁窗口 */
    WS_OP_SUBMIT          = 0x202, /* 提交脏区域 */
    WS_OP_MOVE_WINDOW     = 0x203, /* 移动窗口 */
    WS_OP_SET_TITLE       = 0x205, /* 设置标题 */
    WS_OP_GET_SCREEN_INFO = 0x206, /* 查询屏幕信息 */

    /* 阻塞事件等待 (延迟回复) */
    WS_OP_WAIT_EVENT      = 0x210,

    /* Console/session manager -> wsd */
    WS_OP_SET_ACTIVE      = 0x220, /* data[1]=0/1 */
};

/**
 * 事件类型 (通过 WS_OP_WAIT_EVENT 回复返回)
 */
enum ws_event_type {
    WS_EVENT_INPUT = 1, /* 输入事件 (键盘/鼠标) */
    WS_EVENT_FOCUS = 2, /* 焦点变化 */
    WS_EVENT_CLOSE = 3, /* 关闭请求 */
};

/*
 * 协议常量
 */
#define WS_TITLE_MAX       64  /* 标题最大长度 (含 NUL) */
#define WS_MAX_WINDOWS     32  /* 最大窗口数 */
#define WS_TITLEBAR_HEIGHT 24  /* 标题栏高度 (像素) */
#define WS_BORDER_WIDTH    1   /* 边框宽度 (像素) */
#define WS_CLOSE_BTN_SIZE  16  /* 关闭按钮尺寸 */

/* 窗口创建标志 */
#define WS_FLAG_NO_DECOR (1 << 0) /* 无装饰 (无标题栏/边框) */

/*
 * 消息寄存器布局
 *
 * WS_OP_CREATE_WINDOW:
 *   请求: data[0]=opcode, data[1]=width, data[2]=height, data[3]=flags,
 *         data[4]=client_pid
 *         buffer=title (UTF-8, max WS_TITLE_MAX-1 bytes + NUL)
 *   回复: data[0]=0/err, data[1]=window_id, data[2]=actual_w, data[3]=actual_h
 *         data[4]=shm_size
 *         handles[0]=shm_handle
 *
 * WS_OP_DESTROY_WINDOW:
 *   请求: data[0]=opcode, data[1]=window_id
 *   回复: data[0]=0/err
 *
 * WS_OP_SUBMIT:
 *   请求: data[0]=opcode, data[1]=window_id, data[2]=x, data[3]=y,
 *         data[4]=w, data[5]=h
 *   回复: data[0]=0/err
 *
 * WS_OP_MOVE_WINDOW:
 *   请求: data[0]=opcode, data[1]=window_id, data[2]=new_x, data[3]=new_y
 *   回复: data[0]=0/err
 *
 * WS_OP_SET_TITLE:
 *   请求: data[0]=opcode, data[1]=window_id
 *         buffer=title (UTF-8)
 *   回复: data[0]=0/err
 *
 * WS_OP_GET_SCREEN_INFO:
 *   请求: data[0]=opcode
 *   回复: data[0]=0, data[1]=screen_w, data[2]=screen_h, data[3]=bpp
 *
 * WS_OP_WAIT_EVENT (延迟回复):
 *   请求: data[0]=opcode, data[1]=window_id
 *   回复: data[0]=event_type (ws_event_type)
 *         WS_EVENT_INPUT:
 *           data[1]=window_id
 *           data[2]=INPUT_PACK_REG1 (type|modifiers<<8|code<<16)
 *           data[3]=INPUT_PACK_REG2 ((uint16_t)value|(uint16_t)value2<<16)
 *           data[4]=timestamp
 *         WS_EVENT_FOCUS:
 *           data[1]=window_id, data[2]=1(gained)/0(lost)
 *         WS_EVENT_CLOSE:
 *           data[1]=window_id
 */

/*
 * 装饰颜色 (ARGB32)
 */
#define WS_COLOR_DESKTOP          0xFF336699
#define WS_COLOR_TITLEBAR_ACTIVE  0xFF4488CC
#define WS_COLOR_TITLEBAR_INACTIVE 0xFF666666
#define WS_COLOR_TITLE_TEXT       0xFFFFFFFF
#define WS_COLOR_BORDER           0xFF222222
#define WS_COLOR_CLOSE_BG         0xFFCC4444
#define WS_COLOR_CLOSE_FG         0xFFFFFFFF

#endif /* XNIX_PROTOCOL_WS_H */
