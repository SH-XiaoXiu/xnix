/**
 * @file ramfs.c
 * @brief 内存文件系统实现
 */

#include "ramfs.h"

#include <stdlib.h>
#include <string.h>
#include <xnix/errno.h>

/* 分配节点 */
static struct ramfs_node *alloc_node(struct ramfs_ctx *ctx) {
    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!ctx->nodes[i].in_use) {
            memset(&ctx->nodes[i], 0, sizeof(struct ramfs_node));
            ctx->nodes[i].in_use = true;
            return &ctx->nodes[i];
        }
    }
    return NULL;
}

/* 释放节点 */
static void free_node(struct ramfs_node *node) {
    if (!node) {
        return;
    }
    if (node->data) {
        free(node->data);
        node->data = NULL;
    }
    node->in_use = false;
}

/* 分配句柄 */
static int alloc_handle(struct ramfs_ctx *ctx, struct ramfs_node *node, uint32_t flags) {
    for (int i = 0; i < RAMFS_MAX_HANDLES; i++) {
        if (!ctx->handles[i].in_use) {
            ctx->handles[i].node   = node;
            ctx->handles[i].flags  = flags;
            ctx->handles[i].in_use = true;
            return i;
        }
    }
    return -ENFILE;
}

/* 获取句柄 */
static struct ramfs_handle *get_handle(struct ramfs_ctx *ctx, uint32_t h) {
    if (h >= RAMFS_MAX_HANDLES || !ctx->handles[h].in_use) {
        return NULL;
    }
    return &ctx->handles[h];
}

/* 释放句柄 */
static void free_handle(struct ramfs_ctx *ctx, uint32_t h) {
    if (h < RAMFS_MAX_HANDLES) {
        ctx->handles[h].in_use = false;
    }
}

/* 路径解析: 找到路径指向的节点 */
static struct ramfs_node *lookup_path(struct ramfs_ctx *ctx, const char *path) {
    if (!path || path[0] != '/') {
        return NULL;
    }

    /* 根目录 */
    struct ramfs_node *node = ctx->root;

    /* 如果只是 "/",直接返回根节点 */
    if (path[1] == '\0') {
        return node;
    }

    const char *p = path + 1;
    while (*p) {
        /* 提取下一个路径组件 */
        const char *end = p;
        while (*end && *end != '/') {
            end++;
        }
        size_t len = end - p;
        if (len == 0) {
            p = end + 1;
            continue;
        }

        /* 在当前目录查找 */
        if (node->type != RAMFS_TYPE_DIR) {
            return NULL;
        }

        struct ramfs_node *child = node->children;
        struct ramfs_node *found = NULL;
        while (child) {
            if (strlen(child->name) == len && strncmp(child->name, p, len) == 0) {
                found = child;
                break;
            }
            child = child->next;
        }

        if (!found) {
            return NULL;
        }

        node = found;
        p    = (*end == '/') ? end + 1 : end;
    }

    return node;
}

/* 查找父目录并获取文件名 */
static struct ramfs_node *lookup_parent(struct ramfs_ctx *ctx, const char *path,
                                        const char **out_name, size_t *out_name_len) {
    if (!path || path[0] != '/' || path[1] == '\0') {
        return NULL;
    }

    /* 找最后一个 / */
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    /* 父路径 */
    char   parent_path[VFS_PATH_MAX];
    size_t parent_len = last_slash - path;
    if (parent_len == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        if (parent_len >= VFS_PATH_MAX) {
            return NULL;
        }
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
    }

    *out_name     = last_slash + 1;
    *out_name_len = strlen(*out_name);

    return lookup_path(ctx, parent_path);
}

/* VFS 操作回调(实现接口) */
static int ramfs_open(void *vctx, const char *path, uint32_t flags) {
    struct ramfs_ctx  *ctx  = vctx;
    struct ramfs_node *node = lookup_path(ctx, path);

    if (!node) {
        if (!(flags & VFS_O_CREAT)) {
            return -ENOENT;
        }

        /* 创建文件 */
        const char        *name;
        size_t             name_len;
        struct ramfs_node *parent = lookup_parent(ctx, path, &name, &name_len);
        if (!parent || parent->type != RAMFS_TYPE_DIR) {
            return -ENOENT;
        }
        if (name_len > RAMFS_NAME_MAX) {
            return -ENAMETOOLONG;
        }

        node = alloc_node(ctx);
        if (!node) {
            return -ENOSPC;
        }

        memcpy(node->name, name, name_len);
        node->name[name_len] = '\0';
        node->type           = RAMFS_TYPE_FILE;
        node->size           = 0;
        node->data           = NULL;
        node->capacity       = 0;
        node->parent         = parent;
        node->children       = NULL;
        node->next           = parent->children;
        parent->children     = node;
    } else {
        if (node->type == RAMFS_TYPE_DIR) {
            return -EISDIR;
        }
        if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
            return -EEXIST;
        }
        if (flags & VFS_O_TRUNC) {
            node->size = 0;
        }
    }

    int h = alloc_handle(ctx, node, flags);
    if (h < 0) {
        return h;
    }

    return h;
}

static int ramfs_close(void *vctx, uint32_t handle) {
    struct ramfs_ctx *ctx = vctx;
    if (!get_handle(ctx, handle)) {
        return -EBADF;
    }
    free_handle(ctx, handle);
    return 0;
}

static int ramfs_read(void *vctx, uint32_t handle, void *buf, uint32_t offset, uint32_t size) {
    struct ramfs_ctx    *ctx = vctx;
    struct ramfs_handle *h   = get_handle(ctx, handle);
    if (!h) {
        return -EBADF;
    }

    struct ramfs_node *node = h->node;
    if (node->type == RAMFS_TYPE_DIR) {
        return -EISDIR;
    }

    if (offset >= node->size) {
        return 0;
    }

    uint32_t avail = node->size - offset;
    if (size > avail) {
        size = avail;
    }

    if (size > 0 && node->data) {
        memcpy(buf, node->data + offset, size);
    }

    return (int)size;
}

static int ramfs_write(void *vctx, uint32_t handle, const void *buf, uint32_t offset,
                       uint32_t size) {
    struct ramfs_ctx    *ctx = vctx;
    struct ramfs_handle *h   = get_handle(ctx, handle);
    if (!h) {
        return -EBADF;
    }

    struct ramfs_node *node = h->node;
    if (node->type == RAMFS_TYPE_DIR) {
        return -EISDIR;
    }

    uint32_t end = offset + size;
    if (end > node->capacity) {
        /* 扩展容量 */
        uint32_t new_cap = (end + 4095) & ~4095;
        char    *new_buf = realloc(node->data, new_cap);
        if (!new_buf) {
            return -ENOMEM;
        }
        /* 清零新分配的空间 */
        if (new_cap > node->capacity) {
            memset(new_buf + node->capacity, 0, new_cap - node->capacity);
        }
        node->data     = new_buf;
        node->capacity = new_cap;
    }

    memcpy(node->data + offset, buf, size);
    if (end > node->size) {
        node->size = end;
    }

    return (int)size;
}

static int ramfs_info(void *vctx, const char *path, struct vfs_info *info) {
    struct ramfs_ctx  *ctx  = vctx;
    struct ramfs_node *node = lookup_path(ctx, path);
    if (!node) {
        return -ENOENT;
    }

    memset(info, 0, sizeof(*info));
    info->size = node->size;
    info->type = (node->type == RAMFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;

    return 0;
}

static int ramfs_finfo(void *vctx, uint32_t handle, struct vfs_info *info) {
    struct ramfs_ctx    *ctx = vctx;
    struct ramfs_handle *h   = get_handle(ctx, handle);
    if (!h) {
        return -EBADF;
    }

    struct ramfs_node *node = h->node;
    memset(info, 0, sizeof(*info));
    info->size = node->size;
    info->type = (node->type == RAMFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;

    return 0;
}

static int ramfs_opendir(void *vctx, const char *path) {
    struct ramfs_ctx  *ctx  = vctx;
    struct ramfs_node *node = lookup_path(ctx, path);
    if (!node) {
        return -ENOENT;
    }
    if (node->type != RAMFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    int h = alloc_handle(ctx, node, VFS_O_RDONLY);
    if (h < 0) {
        return h;
    }

    return h;
}

static int ramfs_readdir(void *vctx, uint32_t handle, uint32_t index, struct vfs_dirent *entry) {
    struct ramfs_ctx    *ctx = vctx;
    struct ramfs_handle *h   = get_handle(ctx, handle);
    if (!h) {
        return -EBADF;
    }

    struct ramfs_node *node = h->node;
    if (node->type != RAMFS_TYPE_DIR) {
        return -ENOTDIR;
    }

    /* 遍历到第 index 个子节点 */
    struct ramfs_node *child = node->children;
    for (uint32_t i = 0; i < index && child; i++) {
        child = child->next;
    }

    if (!child) {
        return -ENOENT;
    }

    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, child->name, VFS_NAME_MAX - 1);
    entry->type = (child->type == RAMFS_TYPE_DIR) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    entry->size = (child->type == RAMFS_TYPE_FILE) ? child->size : 0;

    return 0;
}

static int ramfs_mkdir(void *vctx, const char *path) {
    struct ramfs_ctx *ctx = vctx;

    /* 检查是否已存在 */
    if (lookup_path(ctx, path)) {
        return -EEXIST;
    }

    const char        *name;
    size_t             name_len;
    struct ramfs_node *parent = lookup_parent(ctx, path, &name, &name_len);
    if (!parent || parent->type != RAMFS_TYPE_DIR) {
        return -ENOENT;
    }
    if (name_len > RAMFS_NAME_MAX) {
        return -ENAMETOOLONG;
    }

    struct ramfs_node *node = alloc_node(ctx);
    if (!node) {
        return -ENOSPC;
    }

    memcpy(node->name, name, name_len);
    node->name[name_len] = '\0';
    node->type           = RAMFS_TYPE_DIR;
    node->parent         = parent;
    node->children       = NULL;
    node->next           = parent->children;
    parent->children     = node;

    return 0;
}

static int ramfs_del(void *vctx, const char *path) {
    struct ramfs_ctx  *ctx  = vctx;
    struct ramfs_node *node = lookup_path(ctx, path);
    if (!node) {
        return -ENOENT;
    }
    if (node == ctx->root) {
        return -EBUSY;
    }
    if (node->type == RAMFS_TYPE_DIR && node->children) {
        return -ENOTEMPTY;
    }

    /* 从父目录链表移除 */
    struct ramfs_node *parent = node->parent;
    if (parent) {
        struct ramfs_node **pp = &parent->children;
        while (*pp && *pp != node) {
            pp = &(*pp)->next;
        }
        if (*pp) {
            *pp = node->next;
        }
    }

    free_node(node);
    return 0;
}

static int ramfs_truncate(void *vctx, uint32_t handle, uint64_t new_size) {
    struct ramfs_ctx    *ctx = vctx;
    struct ramfs_handle *h   = get_handle(ctx, handle);
    if (!h) {
        return -EBADF;
    }

    struct ramfs_node *node = h->node;
    if (node->type == RAMFS_TYPE_DIR) {
        return -EISDIR;
    }

    if (new_size > node->capacity) {
        uint32_t new_cap = ((uint32_t)new_size + 4095) & ~4095;
        char    *new_buf = realloc(node->data, new_cap);
        if (!new_buf) {
            return -ENOMEM;
        }
        if (new_cap > node->capacity) {
            memset(new_buf + node->capacity, 0, new_cap - node->capacity);
        }
        node->data     = new_buf;
        node->capacity = new_cap;
    }

    node->size = (uint32_t)new_size;
    return 0;
}

static int ramfs_sync(void *vctx, uint32_t handle) {
    struct ramfs_ctx *ctx = vctx;
    if (!get_handle(ctx, handle)) {
        return -EBADF;
    }
    /* 内存文件系统不需要同步 */
    return 0;
}

static int ramfs_rename(void *vctx, const char *old_path, const char *new_path) {
    struct ramfs_ctx  *ctx  = vctx;
    struct ramfs_node *node = lookup_path(ctx, old_path);
    if (!node) {
        return -ENOENT;
    }
    if (node == ctx->root) {
        return -EBUSY;
    }

    /* 检查目标是否已存在 */
    if (lookup_path(ctx, new_path)) {
        return -EEXIST;
    }

    const char        *new_name;
    size_t             new_name_len;
    struct ramfs_node *new_parent = lookup_parent(ctx, new_path, &new_name, &new_name_len);
    if (!new_parent || new_parent->type != RAMFS_TYPE_DIR) {
        return -ENOENT;
    }
    if (new_name_len > RAMFS_NAME_MAX) {
        return -ENAMETOOLONG;
    }

    /* 从旧父目录移除 */
    struct ramfs_node *old_parent = node->parent;
    if (old_parent) {
        struct ramfs_node **pp = &old_parent->children;
        while (*pp && *pp != node) {
            pp = &(*pp)->next;
        }
        if (*pp) {
            *pp = node->next;
        }
    }

    /* 更新名称和父目录 */
    memcpy(node->name, new_name, new_name_len);
    node->name[new_name_len] = '\0';
    node->parent             = new_parent;
    node->next               = new_parent->children;
    new_parent->children     = node;

    return 0;
}

/* 操作接口 */
static struct vfs_operations ramfs_ops = {
    .open     = ramfs_open,
    .close    = ramfs_close,
    .read     = ramfs_read,
    .write    = ramfs_write,
    .info     = ramfs_info,
    .finfo    = ramfs_finfo,
    .opendir  = ramfs_opendir,
    .readdir  = ramfs_readdir,
    .mkdir    = ramfs_mkdir,
    .del      = ramfs_del,
    .truncate = ramfs_truncate,
    .sync     = ramfs_sync,
    .rename   = ramfs_rename,
};

void ramfs_init(struct ramfs_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    /* 创建根目录 */
    ctx->root           = &ctx->nodes[0];
    ctx->root->in_use   = true;
    ctx->root->name[0]  = '/';
    ctx->root->name[1]  = '\0';
    ctx->root->type     = RAMFS_TYPE_DIR;
    ctx->root->parent   = NULL;
    ctx->root->children = NULL;
    ctx->root->next     = NULL;
}

struct vfs_operations *ramfs_get_ops(void) {
    return &ramfs_ops;
}
