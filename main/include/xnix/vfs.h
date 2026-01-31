/**
 * @file vfs.h
 * @brief VFS 公共 API
 *
 * 提供文件系统操作的用户态接口
 */

#ifndef XNIX_VFS_H
#define XNIX_VFS_H

#include <xnix/types.h>
#include <xnix/udm/vfs.h>

/**
 * 初始化 VFS 子系统
 */
void vfs_init(void);

/**
 * 挂载文件系统
 * @param path   挂载点路径(必须以 '/' 开头)
 * @param fs_ep  文件系统服务 endpoint
 * @return 0 成功,负数失败
 */
int vfs_mount(const char *path, cap_handle_t fs_ep);

/**
 * 卸载文件系统
 * @param path 挂载点路径
 * @return 0 成功,负数失败
 */
int vfs_umount(const char *path);

/**
 * 打开文件
 * @param path  文件路径(必须以 '/' 开头)
 * @param flags 打开标志 (VFS_O_*)
 * @return 文件描述符,失败返回负数
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
 * @param fd   文件描述符
 * @param buf  输出缓冲区
 * @param size 读取大小
 * @return 实际读取字节数,0 表示 EOF,负数表示错误
 */
ssize_t vfs_read(int fd, void *buf, size_t size);

/**
 * 写入文件
 * @param fd   文件描述符
 * @param buf  输入数据
 * @param size 写入大小
 * @return 实际写入字节数,负数表示错误
 */
ssize_t vfs_write(int fd, const void *buf, size_t size);

/**
 * 调整文件偏移
 * @param fd     文件描述符
 * @param offset 偏移量
 * @param whence VFS_SEEK_SET/CUR/END
 * @return 新偏移量,失败返回负数
 */
ssize_t vfs_lseek(int fd, ssize_t offset, int whence);

/**
 * 获取文件信息(通过路径)
 * @param path 文件路径
 * @param info 输出文件信息
 * @return 0 成功,负数失败
 */
int vfs_info(const char *path, struct vfs_info *info);

/**
 * 获取文件信息(通过 fd)
 * @param fd   文件描述符
 * @param info 输出文件信息
 * @return 0 成功,负数失败
 */
int vfs_finfo(int fd, struct vfs_info *info);

/**
 * 打开目录
 * @param path 目录路径
 * @return 目录 fd,失败返回负数
 */
int vfs_opendir(const char *path);

/**
 * 读取目录项
 * @param fd    目录 fd
 * @param index 目录项索引(从 0 开始)
 * @param entry 输出目录项
 * @return 0 成功,-ENOENT 无更多项,其他负数失败
 */
int vfs_readdir(int fd, uint32_t index, struct vfs_dirent *entry);

/**
 * 创建目录
 * @param path 目录路径
 * @return 0 成功,负数失败
 */
int vfs_mkdir(const char *path);

/**
 * 删除文件或空目录
 * @param path 文件路径
 * @return 0 成功,负数失败
 */
int vfs_del(const char *path);

#endif /* XNIX_VFS_H */
