/**
 * @file udm/protocol/vfs.h
 * @brief VFS Protocol Definition (userspace only)
 *
 * Defines IPC protocol between VFS clients (libvfs) and FS drivers.
 * Used by both client and server code.
 */

#ifndef UDM_PROTOCOL_VFS_H
#define UDM_PROTOCOL_VFS_H

#include <stdint.h>
#include <xnix/abi/protocol.h>

/* VFS Protocol Operation Codes */
#define UDM_VFS_OPEN     1
#define UDM_VFS_CLOSE    2
#define UDM_VFS_READ     3
#define UDM_VFS_WRITE    4
#define UDM_VFS_INFO     5
#define UDM_VFS_FINFO    6
#define UDM_VFS_OPENDIR  7
#define UDM_VFS_READDIR  8
#define UDM_VFS_MKDIR    9
#define UDM_VFS_DEL      10
#define UDM_VFS_TRUNCATE 11
#define UDM_VFS_SYNC     12
#define UDM_VFS_RENAME   13
#define UDM_VFS_CHDIR    14 /* 改变当前工作目录 */
#define UDM_VFS_GETCWD   15 /* 获取当前工作目录 */
#define UDM_VFS_COPY_CWD 16 /* 复制CWD到子进程 */

/* Helper macros for message parsing */
#define UDM_MSG_OPCODE(msg) ((msg)->regs.data[0])
#define UDM_MSG_ARG(msg, n) ((msg)->regs.data[(n) + 1])

/* VFS Constants */
#define VFS_PATH_MAX 256
#define VFS_NAME_MAX 64

/* File open flags */
#define VFS_O_RDONLY 0x0000
#define VFS_O_WRONLY 0x0001
#define VFS_O_RDWR   0x0002
#define VFS_O_CREAT  0x0100
#define VFS_O_TRUNC  0x0200
#define VFS_O_APPEND 0x0400
#define VFS_O_EXCL   0x0800

/* File types */
#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR  2

/* File info structure */
struct vfs_info {
    uint32_t type;
    uint32_t size;
    uint32_t reserved1;
    uint32_t reserved2;
};

/* Directory entry */
struct vfs_dirent {
    char     name[VFS_NAME_MAX];
    uint32_t type;
    uint32_t size;
};

#endif /* UDM_PROTOCOL_VFS_H */
