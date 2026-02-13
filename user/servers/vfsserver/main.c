/**
 * @file main.c
 * @brief VFS 服务器 - 维护全局挂载表和路径解析
 */

#include <d/protocol/vfs.h>
#include <d/server.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/ipc.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define VFS_MAX_MOUNTS 16

struct vfs_dir_state {
    uint32_t backend_ep;
    uint32_t backend_handle;
    uint32_t mount_count;
    uint32_t backend_skip; /* 被挂载点遮盖而跳过的后端条目数 */
    char     mount_names[VFS_MAX_MOUNTS][VFS_NAME_MAX];
    int      active;
};

static struct vfs_dir_state dir_table[VFS_MAX_MOUNTS];

struct vfs_mount {
    char     path[VFS_PATH_MAX];
    uint32_t path_len;
    uint32_t fs_ep;
    int      active;
};

static struct vfs_mount  mount_table[VFS_MAX_MOUNTS];
static struct vfs_dirent g_reply_dirent;
static handle_t          g_vfs_ep = HANDLE_INVALID;

/* 进程工作目录映射表 */
#define VFS_MAX_PROCESSES 64
struct vfs_cwd_entry {
    uint32_t pid;
    char     cwd[VFS_PATH_MAX];
    int      active;
};

static struct vfs_cwd_entry cwd_table[VFS_MAX_PROCESSES];

static int vfsd_dir_alloc(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!dir_table[i].active) {
            dir_table[i].active       = 1;
            dir_table[i].mount_count  = 0;
            dir_table[i].backend_skip = 0;
            return i;
        }
    }
    return -12; /* ENOMEM */
}

static struct vfs_dir_state *vfsd_dir_get(uint32_t h) {
    if (h >= VFS_MAX_MOUNTS) {
        return NULL;
    }
    if (!dir_table[h].active) {
        return NULL;
    }
    return &dir_table[h];
}

static void vfsd_dir_free(uint32_t h) {
    if (h < VFS_MAX_MOUNTS) {
        dir_table[h].active      = 0;
        dir_table[h].mount_count = 0;
    }
}

/**
 * 获取进程的当前工作目录
 */
static const char *vfsd_get_cwd(uint32_t pid) {
    for (int i = 0; i < VFS_MAX_PROCESSES; i++) {
        if (cwd_table[i].active && cwd_table[i].pid == pid) {
            return cwd_table[i].cwd;
        }
    }
    return "/"; /* 默认返回根目录 */
}

/**
 * 设置进程的当前工作目录
 */
static int vfsd_set_cwd(uint32_t pid, const char *path) {
    if (!path || path[0] != '/') {
        return -22; /* EINVAL */
    }

    /* 查找已有条目 */
    for (int i = 0; i < VFS_MAX_PROCESSES; i++) {
        if (cwd_table[i].active && cwd_table[i].pid == pid) {
            strncpy(cwd_table[i].cwd, path, VFS_PATH_MAX - 1);
            cwd_table[i].cwd[VFS_PATH_MAX - 1] = '\0';
            return 0;
        }
    }

    /* 分配新条目 */
    for (int i = 0; i < VFS_MAX_PROCESSES; i++) {
        if (!cwd_table[i].active) {
            cwd_table[i].active = 1;
            cwd_table[i].pid    = pid;
            strncpy(cwd_table[i].cwd, path, VFS_PATH_MAX - 1);
            cwd_table[i].cwd[VFS_PATH_MAX - 1] = '\0';
            return 0;
        }
    }

    return -12; /* ENOMEM */
}

/**
 * 解析路径(相对路径 → 绝对路径)
 * 基于进程的 CWD
 */
static void vfsd_resolve_path(uint32_t pid, const char *in, char *out, size_t out_size) {
    if (!in || !out || out_size == 0) {
        if (out && out_size > 0) {
            out[0] = '\0';
        }
        return;
    }

    char temp[VFS_PATH_MAX];

    if (in[0] == '/') {
        /* 绝对路径,直接使用 */
        strncpy(temp, in, VFS_PATH_MAX - 1);
    } else {
        /* 相对路径,拼接 CWD */
        const char *cwd = vfsd_get_cwd(pid);
        snprintf(temp, VFS_PATH_MAX, "%s/%s", cwd, in);
    }
    temp[VFS_PATH_MAX - 1] = '\0';

    /* 简化路径 (处理 . 和 ..) */
    char *stack[32];
    int   top    = 0;
    char *cursor = temp;

    while (*cursor == '/') {
        cursor++;
    }

    while (*cursor) {
        char *end = strchr(cursor, '/');
        if (end) {
            *end = '\0';
        }

        if (strcmp(cursor, ".") == 0) {
            /* 跳过 */
        } else if (strcmp(cursor, "..") == 0) {
            if (top > 0) {
                top--;
            }
        } else if (*cursor != '\0') {
            if (top < 32) {
                stack[top++] = cursor;
            }
        }

        if (!end) {
            break;
        }
        cursor = end + 1;
        while (*cursor == '/') {
            cursor++;
        }
    }

    if (top == 0) {
        strncpy(out, "/", out_size);
        return;
    }

    out[0] = '\0';
    for (int i = 0; i < top; i++) {
        size_t len = strlen(out);
        snprintf(out + len, out_size - len, "/%s", stack[i]);
    }
}

static void vfsd_collect_mount_children(const char *base, struct vfs_dir_state *st) {
    if (!base || !st) {
        return;
    }

    size_t base_len = strlen(base);
    int    is_root  = (base_len == 1 && base[0] == '/');

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mount_table[i].active) {
            continue;
        }

        const char *mp = mount_table[i].path;
        if (!mp || mp[0] != '/') {
            continue;
        }

        if (strcmp(mp, base) == 0) {
            continue;
        }

        const char *rem = NULL;
        if (is_root) {
            rem = mp + 1;
        } else {
            if (strncmp(mp, base, base_len) != 0) {
                continue;
            }
            if (mp[base_len] != '/') {
                continue;
            }
            rem = mp + base_len + 1;
        }

        if (!rem || !*rem) {
            continue;
        }

        const char *slash = strchr(rem, '/');
        if (slash) {
            continue;
        }

        if (st->mount_count >= VFS_MAX_MOUNTS) {
            break;
        }

        size_t nlen = strlen(rem);
        if (nlen >= VFS_NAME_MAX) {
            nlen = VFS_NAME_MAX - 1;
        }
        memcpy(st->mount_names[st->mount_count], rem, nlen);
        st->mount_names[st->mount_count][nlen] = '\0';
        st->mount_count++;
    }
}

/**
 * 注册挂载点
 */
static int vfsd_mount(const char *path, uint32_t fs_ep) {
    if (!path || path[0] != '/') {
        return -22; /* EINVAL */
    }

    size_t len = strlen(path);
    if (len == 0 || len >= VFS_PATH_MAX) {
        return -22;
    }

    /* remount: 如果路径已挂载, 替换 fs_ep */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mount_table[i].active && strcmp(mount_table[i].path, path) == 0) {
            mount_table[i].fs_ep = fs_ep;
            return 0;
        }
    }

    /* 新挂载: 分配空闲 slot */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mount_table[i].active) {
            memcpy(mount_table[i].path, path, len + 1);
            mount_table[i].path_len = len;
            mount_table[i].fs_ep    = fs_ep;
            mount_table[i].active   = 1;
            return 0;
        }
    }

    return -12; /* ENOMEM */
}

/**
 * 查找挂载点 (最长前缀匹配)
 * 返回 fs_ep,并将相对路径写入 rel_path_out
 */
static int vfsd_lookup(const char *path, char *rel_path_out, size_t max_len) {
    if (!path || path[0] != '/') {
        return -22;
    }

    struct vfs_mount *best_match = NULL;
    uint32_t          best_len   = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mount_table[i].active) {
            continue;
        }

        uint32_t mlen = mount_table[i].path_len;
        if (mlen > best_len) {
            int match = 1;
            for (uint32_t j = 0; j < mlen; j++) {
                if (path[j] != mount_table[i].path[j]) {
                    match = 0;
                    break;
                }
            }

            int is_root = (mlen == 1 && mount_table[i].path[0] == '/');
            if (match && (is_root || path[mlen] == '/' || path[mlen] == '\0')) {
                best_match = &mount_table[i];
                best_len   = mlen;
            }
        }
    }

    if (!best_match) {
        return -2; /* ENOENT */
    }

    /* 计算相对路径 */
    const char *rel = path + best_len;

    /* 构造以 / 开头的路径 */
    if (rel_path_out) {
        if (*rel == '\0') {
            /* 刚好是挂载点 -> / */
            if (max_len < 2) {
                return -36;
            }
            strcpy(rel_path_out, "/");
        } else if (*rel == '/') {
            /* 已经是 / 开头 -> 直接拷贝 */
            size_t rel_len = strlen(rel);
            if (rel_len >= max_len) {
                return -36;
            }
            strcpy(rel_path_out, rel);
        } else {
            /* 挂载点为 / 且有后续路径的情况,需要补 / */
            int needed = snprintf(rel_path_out, max_len, "/%s", rel);
            if (needed < 0 || (size_t)needed >= max_len) {
                return -36;
            }
        }
    }

    return (int)best_match->fs_ep;
}

/**
 * 转发 VFS 操作到具体的 FS 驱动
 */
static int vfsd_forward(struct ipc_message *msg, uint32_t pid, const char *path) {
    char abs_path[VFS_PATH_MAX];
    char rel_path[VFS_PATH_MAX];

    /* 解析路径 */
    vfsd_resolve_path(pid, path, abs_path, sizeof(abs_path));

    int fs_ep = vfsd_lookup(abs_path, rel_path, sizeof(rel_path));

    if (fs_ep < 0) {
        return fs_ep;
    }

    /* 替换 buffer 为相对路径 */
    msg->buffer.data = (uint64_t)(uintptr_t)rel_path;
    msg->buffer.size = strlen(rel_path);

    /* 调整消息格式:移除 PID,使参数从 data[1] 开始 */
    uint32_t op = msg->regs.data[0];
    if (op == UDM_VFS_OPEN) {
        /* data[1] 是 PID,data[2] 是 flags -> 移动 flags 到 data[1] */
        msg->regs.data[1] = msg->regs.data[2];
    } else if (op == UDM_VFS_MKDIR || op == UDM_VFS_DEL || op == UDM_VFS_INFO ||
               op == UDM_VFS_OPENDIR) {
        /* 这些操作只有 PID,移除它 */
        /* data[1] 是 PID -> 无需额外参数 */
    }

    /* 转发给 FS 驱动 */
    struct ipc_message reply = {0};
    int                ret   = sys_ipc_call(fs_ep, msg, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    /* 将回复内容拷贝回原 msg */
    memcpy(msg->regs.data, reply.regs.data, sizeof(msg->regs.data));
    msg->buffer = reply.buffer;

    /* 对于 OPEN 操作,返回 fs_ep 供客户端后续直接通信 (必须通过 msg.handles 传递) */
    if (op == UDM_VFS_OPEN || op == UDM_VFS_OPENDIR) {
        msg->handles.handles[0] = fs_ep;
        msg->handles.count      = 1;
    }

    return 0;
}

static int vfsd_opendir(struct ipc_message *msg, const char *abs_path) {
    char rel_path[VFS_PATH_MAX];
    int  backend_ep = vfsd_lookup(abs_path, rel_path, sizeof(rel_path));
    if (backend_ep < 0) {
        return backend_ep;
    }

    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_OPENDIR;
    req.buffer.data  = (uint64_t)(uintptr_t)rel_path;
    req.buffer.size  = (uint32_t)strlen(rel_path);

    int ret = sys_ipc_call((uint32_t)backend_ep, &req, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        return result;
    }

    int h = vfsd_dir_alloc();
    if (h < 0) {
        return h;
    }

    struct vfs_dir_state *st = vfsd_dir_get((uint32_t)h);
    st->backend_ep           = (uint32_t)backend_ep;
    st->backend_handle       = (uint32_t)result;
    vfsd_collect_mount_children(abs_path, st);

    msg->regs.data[0]       = UDM_VFS_OPENDIR;
    msg->regs.data[1]       = (uint32_t)h;
    msg->handles.handles[0] = g_vfs_ep;
    msg->handles.count      = 1;
    msg->buffer.data        = (uint64_t)(uintptr_t)0;
    msg->buffer.size        = 0;
    return 0;
}

static int vfsd_readdir(struct ipc_message *msg, uint32_t h, uint32_t index) {
    struct vfs_dir_state *st = vfsd_dir_get(h);
    if (!st) {
        return -22;
    }

    if (index < st->mount_count) {
        memset(&g_reply_dirent, 0, sizeof(g_reply_dirent));
        strncpy(g_reply_dirent.name, st->mount_names[index], VFS_NAME_MAX - 1);
        g_reply_dirent.name[VFS_NAME_MAX - 1] = '\0';
        g_reply_dirent.type                   = VFS_TYPE_DIR;
        g_reply_dirent.size                   = 0;

        msg->regs.data[0] = UDM_VFS_READDIR;
        msg->regs.data[1] = 0;
        msg->buffer.data  = (uint64_t)(uintptr_t)&g_reply_dirent;
        msg->buffer.size  = sizeof(g_reply_dirent);
        return 0;
    }

    uint32_t backend_index = index - st->mount_count + st->backend_skip;

    for (;;) {
        struct ipc_message req   = {0};
        struct ipc_message reply = {0};

        req.regs.data[0] = UDM_VFS_READDIR;
        req.regs.data[1] = st->backend_handle;
        req.regs.data[2] = backend_index;

        reply.buffer.data = (uint64_t)(uintptr_t)&g_reply_dirent;
        reply.buffer.size = sizeof(g_reply_dirent);

        int ret = sys_ipc_call(st->backend_ep, &req, &reply, 5000);
        if (ret < 0) {
            return ret;
        }

        int32_t result = (int32_t)reply.regs.data[1];
        if (result != 0) {
            msg->regs.data[0] = UDM_VFS_READDIR;
            msg->regs.data[1] = reply.regs.data[1];
            msg->buffer.data  = (uint64_t)(uintptr_t)0;
            msg->buffer.size  = 0;
            return 0;
        }

        /* 检查是否被挂载点遮盖 */
        int shadowed = 0;
        for (uint32_t i = 0; i < st->mount_count; i++) {
            if (strcmp(g_reply_dirent.name, st->mount_names[i]) == 0) {
                shadowed = 1;
                break;
            }
        }

        if (!shadowed) {
            break;
        }

        /* 被遮盖,跳过并继续读下一个后端条目 */
        st->backend_skip++;
        backend_index++;
    }

    msg->regs.data[0] = UDM_VFS_READDIR;
    msg->regs.data[1] = 0;
    msg->buffer.data  = (uint64_t)(uintptr_t)&g_reply_dirent;
    msg->buffer.size  = sizeof(g_reply_dirent);
    return 0;
}

static int vfsd_close_handle(struct ipc_message *msg, uint32_t h) {
    struct vfs_dir_state *st = vfsd_dir_get(h);
    if (!st) {
        return -22;
    }

    struct ipc_message req   = {0};
    struct ipc_message reply = {0};

    req.regs.data[0] = UDM_VFS_CLOSE;
    req.regs.data[1] = st->backend_handle;

    int ret = sys_ipc_call(st->backend_ep, &req, &reply, 5000);
    if (ret < 0) {
        return ret;
    }

    int32_t result = (int32_t)reply.regs.data[1];
    vfsd_dir_free(h);

    msg->regs.data[0]  = UDM_VFS_CLOSE;
    msg->regs.data[1]  = (uint32_t)result;
    msg->buffer.data   = (uint64_t)(uintptr_t)0;
    msg->buffer.size   = 0;
    msg->handles.count = 0;
    return 0;
}

/**
 * VFS 消息处理
 */
static int vfsd_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    /* CHDIR: 改变当前工作目录 */
    if (op == UDM_VFS_CHDIR) {
        uint32_t pid = UDM_MSG_ARG(msg, 0);
        char     path[VFS_PATH_MAX];
        char     abs_path[VFS_PATH_MAX];

        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(path, (void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            path[msg->buffer.size] = '\0';

            /* 解析为绝对路径 */
            vfsd_resolve_path(pid, path, abs_path, sizeof(abs_path));

            /* 验证路径存在且是目录(通过 stat 转发给 FS 驱动)*/
            char rel_path[VFS_PATH_MAX];
            int  fs_ep = vfsd_lookup(abs_path, rel_path, sizeof(rel_path));
            if (fs_ep < 0) {
                msg->regs.data[0] = op;
                msg->regs.data[1] = (uint32_t)fs_ep;
                return 0;
            }

            /* 转发 INFO 操作验证是否是目录 */
            struct ipc_message stat_req   = {0};
            struct ipc_message stat_reply = {0};
            stat_req.regs.data[0]         = UDM_VFS_INFO;
            stat_req.buffer.data          = (uint64_t)(uintptr_t)rel_path;
            stat_req.buffer.size          = strlen(rel_path);

            int ret = sys_ipc_call(fs_ep, &stat_req, &stat_reply, 5000);
            if (ret < 0 || (int32_t)stat_reply.regs.data[1] < 0) {
                msg->regs.data[0] = op;
                msg->regs.data[1] = ret < 0 ? (uint32_t)ret : stat_reply.regs.data[1];
                return 0;
            }

            uint32_t type = stat_reply.regs.data[3];
            if (type != VFS_TYPE_DIR) {
                msg->regs.data[0] = op;
                msg->regs.data[1] = (uint32_t)-20; /* ENOTDIR */
                return 0;
            }

            /* 设置 CWD (使用绝对路径) */
            int cwd_ret       = vfsd_set_cwd(pid, abs_path);
            msg->regs.data[0] = op;
            msg->regs.data[1] = (uint32_t)cwd_ret;
            return 0;
        }

        msg->regs.data[0] = op;
        msg->regs.data[1] = (uint32_t)-22; /* EINVAL */
        return 0;
    }

    /* GETCWD: 获取当前工作目录 */
    if (op == UDM_VFS_GETCWD) {
        uint32_t    pid = UDM_MSG_ARG(msg, 0);
        const char *cwd = vfsd_get_cwd(pid);

        msg->regs.data[0] = op;
        msg->regs.data[1] = 0; /* success */
        msg->buffer.data  = (uint64_t)(uintptr_t)(void *)cwd;
        msg->buffer.size  = strlen(cwd);
        return 0;
    }

    /* COPY_CWD: 复制调用者的CWD到子进程 */
    if (op == UDM_VFS_COPY_CWD) {
        uint32_t parent_pid = UDM_MSG_ARG(msg, 0); /* 调用者 PID */
        uint32_t child_pid  = UDM_MSG_ARG(msg, 1); /* 子进程 PID */

        const char *parent_cwd = vfsd_get_cwd(parent_pid);
        int         ret        = vfsd_set_cwd(child_pid, parent_cwd);

        msg->regs.data[0] = op;
        msg->regs.data[1] = (uint32_t)ret;
        return 0;
    }

    /* 特殊操作:VFS_MOUNT (注册挂载点) */
    if (op == 0x1000) { /* VFS_MOUNT */
        char path[VFS_PATH_MAX];

        /* 从 IPC handles 中提取 FS endpoint handle */
        if (msg->handles.count < 1) {
            msg->regs.data[0] = op;
            msg->regs.data[1] = (uint32_t)-22; /* EINVAL */
            return 0;
        }

        uint32_t fs_ep = msg->handles.handles[0]; /* vfsd 现在拥有这个 handle */

        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(path, (void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            path[msg->buffer.size] = '\0';

            int ret = vfsd_mount(path, fs_ep);
            if (ret == 0) {
                ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[vfsd]", " mounted %s\n", path);
            } else {
                ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[vfsd]", " mount failed for %s: %d\n",
                          path, ret);
            }
            msg->regs.data[0] = op;
            msg->regs.data[1] = (uint32_t)ret;
            return 0;
        }

        msg->regs.data[0] = op;
        msg->regs.data[1] = (uint32_t)-22;
        return 0;
    }

    if (op == UDM_VFS_OPENDIR) {
        uint32_t pid = UDM_MSG_ARG(msg, 0);
        char     rel_path[VFS_PATH_MAX];
        char     abs_path[VFS_PATH_MAX];

        if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
            memcpy(rel_path, (void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
            rel_path[msg->buffer.size] = '\0';

            /* 解析为绝对路径 */
            vfsd_resolve_path(pid, rel_path, abs_path, sizeof(abs_path));

            return vfsd_opendir(msg, abs_path);
        }
        msg->regs.data[0] = op;
        msg->regs.data[1] = (uint32_t)-22;
        return 0;
    }

    if (op == UDM_VFS_READDIR) {
        uint32_t h     = UDM_MSG_ARG(msg, 0);
        uint32_t index = UDM_MSG_ARG(msg, 1);
        int      ret   = vfsd_readdir(msg, h, index);
        if (ret < 0) {
            msg->regs.data[0] = op;
            msg->regs.data[1] = (uint32_t)ret;
            msg->buffer.data  = (uint64_t)(uintptr_t)0;
            msg->buffer.size  = 0;
        }
        return 0;
    }

    if (op == UDM_VFS_CLOSE) {
        uint32_t h = UDM_MSG_ARG(msg, 0);
        if (vfsd_dir_get(h)) {
            int ret = vfsd_close_handle(msg, h);
            if (ret < 0) {
                msg->regs.data[0] = op;
                msg->regs.data[1] = (uint32_t)ret;
            }
            return 0;
        }
    }

    /* 其他操作:需要路径解析 */
    uint32_t pid = UDM_MSG_ARG(msg, 0);
    char     path[VFS_PATH_MAX];

    if (msg->buffer.data && msg->buffer.size > 0 && msg->buffer.size < VFS_PATH_MAX) {
        memcpy(path, (void *)(uintptr_t)msg->buffer.data, msg->buffer.size);
        path[msg->buffer.size] = '\0';

        int ret = vfsd_forward(msg, pid, path);
        if (ret < 0) {
            msg->regs.data[0] = op;
            msg->regs.data[1] = (uint32_t)ret;
        }
        return 0;
    }

    msg->regs.data[0] = op;
    msg->regs.data[1] = (uint32_t)-22;
    return 0;
}

int main(void) {
    g_vfs_ep = env_get_handle("vfs_ep");
    if (g_vfs_ep == HANDLE_INVALID) {
        return 1;
    }

    /* 初始化挂载表 */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mount_table[i].active = 0;
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        dir_table[i].active      = 0;
        dir_table[i].mount_count = 0;
    }
    for (int i = 0; i < VFS_MAX_PROCESSES; i++) {
        cwd_table[i].active = 0;
    }

    struct udm_server srv = {
        .endpoint = g_vfs_ep,
        .handler  = vfsd_handler,
        .name     = "vfsd",
    };

    udm_server_init(&srv);
    svc_notify_ready("vfsserver");
    udm_server_run(&srv);

    return 0;
}
