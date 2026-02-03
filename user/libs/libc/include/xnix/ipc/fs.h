/**
 * @file xnix/ipc/fs.h
 * @brief 文件系统 IPC 协议定义
 *
 * 定义用户态 VFS 库与文件系统驱动之间的通信协议.
 */

#ifndef XNIX_IPC_FS_H
#define XNIX_IPC_FS_H

#include <stdint.h>

/**
 * 文件系统操作码
 */
typedef enum {
    FS_OP_OPEN    = 1,  /* 打开文件 */
    FS_OP_CLOSE   = 2,  /* 关闭文件 */
    FS_OP_READ    = 3,  /* 读取文件 */
    FS_OP_WRITE   = 4,  /* 写入文件 */
    FS_OP_SEEK    = 5,  /* 调整偏移 */
    FS_OP_STAT    = 6,  /* 获取文件信息 */
    FS_OP_OPENDIR = 7,  /* 打开目录 */
    FS_OP_READDIR = 8,  /* 读取目录项 */
    FS_OP_MKDIR   = 9,  /* 创建目录 */
    FS_OP_DELETE  = 10, /* 删除文件/目录 */
} fs_op_t;

/**
 * 文件打开标志
 */
#define FS_O_RDONLY 0x0000 /* 只读 */
#define FS_O_WRONLY 0x0001 /* 只写 */
#define FS_O_RDWR   0x0002 /* 读写 */
#define FS_O_CREAT  0x0100 /* 不存在则创建 */
#define FS_O_TRUNC  0x0200 /* 截断为 0 */
#define FS_O_APPEND 0x0400 /* 追加模式 */

/**
 * Seek 模式
 */
#define FS_SEEK_SET 0 /* 从文件开头 */
#define FS_SEEK_CUR 1 /* 从当前位置 */
#define FS_SEEK_END 2 /* 从文件末尾 */

/**
 * 文件类型
 */
#define FS_TYPE_FILE 1 /* 普通文件 */
#define FS_TYPE_DIR  2 /* 目录 */

/**
 * 标准 IPC 消息格式 (文件系统)
 *
 * 请求格式:
 *   - data[0]: op_code (fs_op_t)
 *   - data[1-7]: 操作相关参数
 *
 * 回复格式:
 *   - data[0]: result (0 成功, <0 错误码)
 *   - data[1-7]: 返回数据
 */
struct fs_ipc_request {
    uint32_t op_code; /* 操作码 (fs_op_t) */
    uint32_t handle;  /* 文件句柄 */
    uint32_t flags;   /* 标志位 */
    uint32_t offset;  /* 偏移量 */
    uint32_t size;    /* 数据大小 */
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct fs_ipc_response {
    int32_t  result;    /* 0 成功, <0 错误码 */
    uint32_t handle;    /* 返回的文件句柄 */
    uint32_t size;      /* 实际读写字节数 */
    uint32_t file_size; /* 文件大小 */
    uint32_t file_type; /* 文件类型 */
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

/**
 * 路径最大长度
 */
#define FS_PATH_MAX 256

/**
 * 文件名最大长度
 */
#define FS_NAME_MAX 64

/**
 * 目录项
 */
struct fs_dirent {
    char     name[FS_NAME_MAX]; /* 文件名 */
    uint32_t type;              /* 文件类型 */
    uint32_t size;              /* 文件大小 */
};

/**
 * 文件信息
 */
struct fs_stat {
    uint32_t type; /* 文件类型 */
    uint32_t size; /* 文件大小 */
    uint32_t reserved1;
    uint32_t reserved2;
};

#endif /* XNIX_IPC_FS_H */
