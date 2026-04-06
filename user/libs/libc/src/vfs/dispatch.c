/**
 * @file dispatch.c
 * @brief VFS 服务端消息分发
 */

#include <stdio.h>
#include <string.h>
#include <vfs/vfs.h>
#include <xnix/abi/io.h>
#include <xnix/ipc.h>
#include <xnix/protocol/vfs.h>
#include <xnix/syscall.h>

#define VFS_BUF_SIZE 4096

static char              g_path_buf[VFS_PATH_MAX];
static char              g_data_buf[VFS_BUF_SIZE];
static struct vfs_info   g_info_buf;
static struct vfs_dirent g_dirent_buf;

int vfs_dispatch(struct vfs_operations *ops, void *ctx, struct ipc_message *msg) {
    if (!ops || !msg) {
        return -1;
    }

    uint32_t op     = UDM_MSG_OPCODE(msg);
    int      result = -38;
    struct ipc_message reply = {0};

    switch (op) {
    case UDM_VFS_OPEN: {
        if (!ops->open) break;
        uint32_t flags = UDM_MSG_ARG(msg, 0);
        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(g_path_buf, (const void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            g_path_buf[msg->buffer.size] = '\0';
            result = ops->open(ctx, g_path_buf, flags);
        } else {
            result = -22;
        }
        break;
    }
    case UDM_VFS_CLOSE: {
        if (!ops->close) break;
        result = ops->close(ctx, UDM_MSG_ARG(msg, 0));
        break;
    }
    case UDM_VFS_READ: {
        if (!ops->read) break;
        uint32_t handle = UDM_MSG_ARG(msg, 0);
        uint32_t offset = UDM_MSG_ARG(msg, 1);
        uint32_t size   = UDM_MSG_ARG(msg, 2);
        if (size > VFS_BUF_SIZE) size = VFS_BUF_SIZE;
        result = ops->read(ctx, handle, g_data_buf, offset, size);
        if (result > 0) {
            reply.buffer.data = (uint64_t)(uintptr_t)g_data_buf;
            reply.buffer.size = (uint32_t)result;
        }
        break;
    }
    case UDM_VFS_WRITE: {
        if (!ops->write) break;
        uint32_t handle = UDM_MSG_ARG(msg, 0);
        uint32_t offset = UDM_MSG_ARG(msg, 1);
        uint32_t size   = UDM_MSG_ARG(msg, 2);
        if (msg->buffer.data && msg->buffer.size > 0) {
            if (size > msg->buffer.size) size = msg->buffer.size;
            if (size > VFS_BUF_SIZE) size = VFS_BUF_SIZE;
            memcpy(g_data_buf, (const void *)(uintptr_t)msg->buffer.data, size);
            result = ops->write(ctx, handle, g_data_buf, offset, size);
        } else {
            result = -22;
        }
        break;
    }
    case UDM_VFS_INFO: {
        if (!ops->info) break;
        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(g_path_buf, (const void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            g_path_buf[msg->buffer.size] = '\0';
            result = ops->info(ctx, g_path_buf, &g_info_buf);
            if (result == 0) {
                msg->regs.data[2] = g_info_buf.size;
                msg->regs.data[3] = g_info_buf.type;
            }
        } else {
            result = -22;
        }
        break;
    }
    case UDM_VFS_FINFO: {
        if (!ops->finfo) break;
        result = ops->finfo(ctx, UDM_MSG_ARG(msg, 0), &g_info_buf);
        if (result == 0) {
            msg->regs.data[2] = g_info_buf.size;
            msg->regs.data[3] = g_info_buf.type;
        }
        break;
    }
    case UDM_VFS_OPENDIR: {
        if (!ops->opendir) break;
        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(g_path_buf, (const void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            g_path_buf[msg->buffer.size] = '\0';
            result = ops->opendir(ctx, g_path_buf);
        } else {
            result = -22;
        }
        break;
    }
    case UDM_VFS_READDIR: {
        if (!ops->readdir) break;
        result = ops->readdir(ctx, UDM_MSG_ARG(msg, 0), UDM_MSG_ARG(msg, 1), &g_dirent_buf);
        if (result == 0) {
            reply.buffer.data = (uint64_t)(uintptr_t)&g_dirent_buf;
            reply.buffer.size = sizeof(g_dirent_buf);
        }
        break;
    }
    case UDM_VFS_MKDIR: {
        if (!ops->mkdir) break;
        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(g_path_buf, (const void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            g_path_buf[msg->buffer.size] = '\0';
            result = ops->mkdir(ctx, g_path_buf);
        } else {
            result = -22;
        }
        break;
    }
    case UDM_VFS_DEL: {
        if (!ops->del) break;
        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(g_path_buf, (const void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            g_path_buf[msg->buffer.size] = '\0';
            result = ops->del(ctx, g_path_buf);
        } else {
            result = -22;
        }
        break;
    }
    case UDM_VFS_TRUNCATE: {
        if (!ops->truncate) break;
        uint64_t new_size = ((uint64_t)UDM_MSG_ARG(msg, 2) << 32) | UDM_MSG_ARG(msg, 1);
        result = ops->truncate(ctx, UDM_MSG_ARG(msg, 0), new_size);
        break;
    }
    case UDM_VFS_SYNC: {
        if (!ops->sync) break;
        result = ops->sync(ctx, UDM_MSG_ARG(msg, 0));
        break;
    }
    case UDM_VFS_RENAME: {
        if (!ops->rename) break;
        uint32_t old_len = UDM_MSG_ARG(msg, 0);
        if (msg->buffer.data && msg->buffer.size > old_len + 1) {
            const char *old_path = (const char *)(uintptr_t)msg->buffer.data;
            const char *new_path = old_path + old_len + 1;
            result = ops->rename(ctx, old_path, new_path);
        } else {
            result = -22;
        }
        break;
    }
    case IO_READ: {
        if (!ops->read) break;
        uint32_t handle = msg->regs.data[1];
        uint32_t offset = msg->regs.data[2];
        uint32_t size   = msg->regs.data[3];
        if (size > VFS_BUF_SIZE) size = VFS_BUF_SIZE;
        result = ops->read(ctx, handle, g_data_buf, offset, size);
        if (result > 0) {
            reply.buffer.data = (uint64_t)(uintptr_t)g_data_buf;
            reply.buffer.size = (uint32_t)result;
        }
        break;
    }
    case IO_WRITE: {
        if (!ops->write) break;
        uint32_t handle = msg->regs.data[1];
        uint32_t offset = msg->regs.data[2];
        uint32_t size   = msg->regs.data[3];
        if (msg->buffer.data && msg->buffer.size > 0) {
            if (size > msg->buffer.size) size = msg->buffer.size;
            if (size > VFS_BUF_SIZE) size = VFS_BUF_SIZE;
            memcpy(g_data_buf, (const void *)(uintptr_t)msg->buffer.data, size);
            result = ops->write(ctx, handle, g_data_buf, offset, size);
        } else {
            result = -22;
        }
        break;
    }
    case IO_CLOSE: {
        if (!ops->close) break;
        result = ops->close(ctx, msg->regs.data[1]);
        break;
    }
    default:
        break;
    }

    reply.regs.data[0] = (uint32_t)result;
    sys_ipc_reply_to(msg->sender_tid, &reply);
    return 0;
}
