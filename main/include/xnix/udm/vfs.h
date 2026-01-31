/**
 * @file vfs.h
 * @brief VFS UDM 协议定义
 *
 * 定义内核 VFS 层与用户态文件系统服务之间的 IPC 协议
 */

#ifndef XNIX_UDM_VFS_H
#define XNIX_UDM_VFS_H

#include <xnix/abi/ipc.h>
#include <xnix/abi/types.h>
#include <xnix/udm/protocol.h>

/**
 * VFS 操作码
 *
 * msg.regs.data[0] = opcode
 * 参数和返回值见各操作码注释
 */
enum udm_vfs_op {
    /**
     * 打开文件
     * 请求: data[1] = flags, buffer = path (null-terminated)
     * 响应: data[1] = handle (成功) 或 error (失败)
     */
    UDM_VFS_OPEN = 1,

    /**
     * 关闭文件
     * 请求: data[1] = handle
     * 响应: data[1] = 0 (成功) 或 error (失败)
     */
    UDM_VFS_CLOSE = 2,

    /**
     * 读取数据
     * 请求: data[1] = handle, data[2] = offset, data[3] = size, buffer = 输出缓冲区
     * 响应: data[1] = 实际读取字节数 或 error (负数)
     */
    UDM_VFS_READ = 3,

    /**
     * 写入数据
     * 请求: data[1] = handle, data[2] = offset, data[3] = size, buffer = 输入数据
     * 响应: data[1] = 实际写入字节数 或 error (负数)
     */
    UDM_VFS_WRITE = 4,

    /**
     * 获取文件信息（通过路径）
     * 请求: buffer = path (null-terminated)
     * 响应: data[1] = 0 (成功) 或 error, buffer = struct vfs_info
     */
    UDM_VFS_INFO = 5,

    /**
     * 读取目录项
     * 请求: data[1] = handle, data[2] = index
     * 响应: data[1] = 0 (成功), -ENOENT (无更多项), 或 error
     *       buffer = struct vfs_dirent
     */
    UDM_VFS_READDIR = 6,

    /**
     * 创建目录
     * 请求: buffer = path (null-terminated)
     * 响应: data[1] = 0 (成功) 或 error
     */
    UDM_VFS_MKDIR = 7,

    /**
     * 删除文件或空目录
     * 请求: buffer = path (null-terminated)
     * 响应: data[1] = 0 (成功) 或 error
     */
    UDM_VFS_DEL = 8,

    /**
     * 获取文件信息（通过 handle）
     * 请求: data[1] = handle
     * 响应: data[1] = 0 (成功) 或 error, buffer = struct vfs_info
     */
    UDM_VFS_FINFO = 9,

    /**
     * 打开目录
     * 请求: buffer = path (null-terminated)
     * 响应: data[1] = handle (成功) 或 error (失败)
     */
    UDM_VFS_OPENDIR = 10,

    /**
     * 重命名/移动文件
     * 请求: data[1] = old_path_len, buffer = old_path + new_path
     * 响应: data[1] = 0 (成功) 或 error
     */
    UDM_VFS_RENAME = 11,

    /**
     * 截断文件
     * 请求: data[1] = handle, data[2..3] = new_size (64-bit)
     * 响应: data[1] = 0 (成功) 或 error
     */
    UDM_VFS_TRUNCATE = 12,

    /**
     * 同步文件到存储
     * 请求: data[1] = handle
     * 响应: data[1] = 0 (成功) 或 error
     */
    UDM_VFS_SYNC = 13,
};

/**
 * 文件打开标志
 */
#define VFS_O_RDONLY    (1 << 0)                      /* 只读 */
#define VFS_O_WRONLY    (1 << 1)                      /* 只写 */
#define VFS_O_RDWR      (VFS_O_RDONLY | VFS_O_WRONLY) /* 读写 */
#define VFS_O_CREAT     (1 << 2)                      /* 不存在则创建 */
#define VFS_O_EXCL      (1 << 3)                      /* 与 CREAT 一起用,文件必须不存在 */
#define VFS_O_TRUNC     (1 << 4)                      /* 截断为零长度 */
#define VFS_O_APPEND    (1 << 5)                      /* 追加模式 */
#define VFS_O_DIRECTORY (1 << 6)                      /* 必须是目录 */

/**
 * 文件类型
 */
enum vfs_file_type {
    VFS_TYPE_UNKNOWN = 0,
    VFS_TYPE_FILE    = 1, /* 普通文件 */
    VFS_TYPE_DIR     = 2, /* 目录 */
    VFS_TYPE_SYMLINK = 3, /* 符号链接 */
    VFS_TYPE_DEVICE  = 4, /* 设备文件 */
};

/**
 * 文件信息结构
 */
struct vfs_info {
    uint32_t type;  /* enum vfs_file_type */
    uint32_t mode;  /* 权限位 (预留) */
    uint64_t size;  /* 文件大小 */
    uint64_t ctime; /* 创建时间 (预留) */
    uint64_t mtime; /* 修改时间 (预留) */
    uint64_t atime; /* 访问时间 (预留) */
};

/**
 * 目录项结构
 */
#define VFS_NAME_MAX 255

struct vfs_dirent {
    uint32_t type;                   /* enum vfs_file_type */
    uint32_t name_len;               /* 名称长度 */
    char     name[VFS_NAME_MAX + 1]; /* 文件名 (null-terminated) */
};

/**
 * 路径最大长度
 */
#define VFS_PATH_MAX 1024

/**
 * lseek whence 参数 (文件偏移量)
 */
#define VFS_SEEK_SET 0 /* 从文件开头 */
#define VFS_SEEK_CUR 1 /* 从当前位置 */
#define VFS_SEEK_END 2 /* 从文件末尾 */

#endif /* XNIX_UDM_VFS_H */
