/**
 * @file vfs_client.h
 * @brief 用户态 VFS 客户端接口
 */

#ifndef VFS_CLIENT_H
#define VFS_CLIENT_H

#include <stddef.h>
#include <stdint.h>

/* ssize_t 定义(避免依赖 sys/types.h) */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef int32_t ssize_t;
#endif

/* Forward declarations */
struct vfs_dirent;

/**
 * 初始化 VFS 客户端
 */
void vfs_client_init(uint32_t vfsd_ep);

/**
 * 挂载文件系统
 * @param path 挂载点路径
 * @param fs_ep 文件系统驱动的 endpoint
 * @return 0 成功,负数失败
 */
int vfs_mount(const char *path, uint32_t fs_ep);

/**
 * 打开文件
 * @param path 文件路径
 * @param flags 打开标志
 * @return 文件描述符,负数失败
 */
int vfs_open(const char *path, uint32_t flags);

/**
 * 关闭文件
 * @param fd 文件描述符
 * @return 0 成功,负数失败
 */
int vfs_close(int fd);

/**
 * 读取文件
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param size 读取大小
 * @return 实际读取字节数,负数失败
 */
ssize_t vfs_read(int fd, void *buf, size_t size);

/**
 * 写入文件
 * @param fd 文件描述符
 * @param buf 数据
 * @param size 写入大小
 * @return 实际写入字节数,负数失败
 */
ssize_t vfs_write(int fd, const void *buf, size_t size);

/**
 * 创建目录
 * @param path 目录路径
 * @return 0 成功,负数失败
 */
int vfs_mkdir(const char *path);

/**
 * 删除文件或目录
 * @param path 路径
 * @return 0 成功,负数失败
 */
int vfs_delete(const char *path);

/**
 * 文件信息结构(简化版)
 */
struct vfs_stat {
    uint32_t size;
    uint32_t type; /* VFS_TYPE_FILE (1) or VFS_TYPE_DIR (2) */
};

/**
 * 获取文件信息
 * @param path 文件路径
 * @param st 输出信息
 * @return 0 成功,负数失败
 */
int vfs_stat(const char *path, struct vfs_stat *st);

/**
 * 打开目录
 * @param path 目录路径
 * @return 目录描述符,负数失败
 */
int vfs_opendir(const char *path);

/**
 * 读取目录项
 * @param fd 目录描述符
 * @param name 输出文件名缓冲区
 * @param size 缓冲区大小
 * @return 1 成功读取,0 结束,-1 失败
 */
int vfs_readdir(int fd, char *name, size_t size);

/**
 * 读取目录项(带索引)
 * @param fd 目录描述符
 * @param index 项索引
 * @param dirent 输出目录项结构
 * @return 0 成功,-1 失败
 */
int vfs_readdir_index(int fd, uint32_t index, struct vfs_dirent *dirent);

/**
 * 改变当前工作目录
 * @param path 目录路径
 * @return 0 成功,负数失败
 */
int vfs_chdir(const char *path);

/**
 * 获取当前工作目录
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 0 成功,负数失败
 */
int vfs_getcwd(char *buf, size_t size);

/**
 * 复制当前进程的CWD到子进程
 * @param child_pid 子进程PID
 * @return 0 成功,负数失败
 */
int vfs_copy_cwd_to_child(int32_t child_pid);

#endif /* VFS_CLIENT_H */
