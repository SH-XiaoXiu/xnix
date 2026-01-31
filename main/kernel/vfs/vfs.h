/**
 * @file vfs.h
 * @brief VFS 内核实现
 *
 * 公共 API 见 <xnix/vfs.h>
 */

#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <xnix/capability.h>
#include <xnix/sync.h>
#include <xnix/types.h>
#include <xnix/udm/vfs.h>

struct process;

/**
 * VFS 配置
 */
#define VFS_MAX_FD     32 /* 每进程最大 fd 数 */
#define VFS_MAX_MOUNTS 16 /* 最大挂载点数 */

/**
 * 打开的文件
 */
struct vfs_file {
    uint32_t     fs_handle; /* fs 服务内部 handle */
    cap_handle_t fs_ep;     /* fs 服务 endpoint */
    uint32_t     offset;    /* 当前偏移 */
    uint32_t     flags;     /* 打开标志 (VFS_O_*) */
    uint32_t     refcount;  /* 引用计数 */
};

/**
 * 文件描述符表
 */
struct fd_table {
    struct vfs_file *files[VFS_MAX_FD];
    spinlock_t       lock;
};

/**
 * 挂载点
 */
struct vfs_mount {
    char         path[VFS_PATH_MAX]; /* 挂载点路径 */
    uint32_t     path_len;           /* 路径长度 */
    cap_handle_t fs_ep;              /* fs 服务 endpoint */
    bool         active;             /* 是否激活 */
};

/**
 * 初始化 VFS 子系统
 */
void vfs_init(void);

/**
 * 创建 fd 表
 */
struct fd_table *fd_table_create(void);

/**
 * 销毁 fd 表，关闭所有打开的文件
 */
void fd_table_destroy(struct fd_table *fdt);

/**
 * 分配 fd
 * @return fd 号，失败返回 -1
 */
int fd_alloc(struct fd_table *fdt);

/**
 * 获取 fd 对应的 vfs_file
 */
struct vfs_file *fd_get(struct fd_table *fdt, int fd);

/**
 * 释放 fd
 */
void fd_free(struct fd_table *fdt, int fd);

/**
 * 挂载文件系统
 * @param path   挂载点路径
 * @param fs_ep  fs 服务 endpoint
 * @return 0 成功，负数失败
 */
int vfs_mount(const char *path, cap_handle_t fs_ep);

/**
 * 卸载文件系统
 * @param path 挂载点路径
 * @return 0 成功，负数失败
 */
int vfs_umount(const char *path);

/**
 * 查找路径对应的挂载点
 * @param path     完整路径
 * @param rel_path 输出相对路径（挂载点之后的部分）
 * @return 挂载点，未找到返回 NULL
 */
struct vfs_mount *vfs_lookup_mount(const char *path, const char **rel_path);

/**
 * 打开文件
 * @param path  文件路径
 * @param flags 打开标志 (VFS_O_*)
 * @return fd，失败返回负数
 */
int vfs_open(const char *path, uint32_t flags);

/**
 * 关闭文件
 * @param fd 文件描述符
 * @return 0 成功，负数失败
 */
int vfs_close(int fd);

/**
 * 读取文件
 * @param fd   文件描述符
 * @param buf  输出缓冲区
 * @param size 读取大小
 * @return 实际读取字节数，失败返回负数
 */
ssize_t vfs_read(int fd, void *buf, size_t size);

/**
 * 写入文件
 * @param fd   文件描述符
 * @param buf  输入数据
 * @param size 写入大小
 * @return 实际写入字节数，失败返回负数
 */
ssize_t vfs_write(int fd, const void *buf, size_t size);

/**
 * 调整文件偏移
 * @param fd     文件描述符
 * @param offset 偏移量
 * @param whence VFS_SEEK_SET/CUR/END
 * @return 新偏移量，失败返回负数
 */
ssize_t vfs_lseek(int fd, ssize_t offset, int whence);

/**
 * 获取文件信息（通过路径）
 * @param path 文件路径
 * @param info 输出文件信息
 * @return 0 成功，负数失败
 */
int vfs_info(const char *path, struct vfs_info *info);

/**
 * 获取文件信息（通过 fd）
 * @param fd   文件描述符
 * @param info 输出文件信息
 * @return 0 成功，负数失败
 */
int vfs_finfo(int fd, struct vfs_info *info);

/**
 * 打开目录
 * @param path 目录路径
 * @return fd，失败返回负数
 */
int vfs_opendir(const char *path);

/**
 * 读取目录项
 * @param fd    目录 fd
 * @param index 目录项索引
 * @param entry 输出目录项
 * @return 0 成功，-ENOENT 无更多项，其他负数失败
 */
int vfs_readdir(int fd, uint32_t index, struct vfs_dirent *entry);

/**
 * 创建目录
 * @param path 目录路径
 * @return 0 成功，负数失败
 */
int vfs_mkdir(const char *path);

/**
 * 删除文件或空目录
 * @param path 文件路径
 * @return 0 成功，负数失败
 */
int vfs_del(const char *path);

#endif /* KERNEL_VFS_H */
