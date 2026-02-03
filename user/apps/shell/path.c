/**
 * @file path.c
 * @brief Shell PATH 管理实现
 */

#include "path.h"

#include "stdio.h"

#include <d/protocol/vfs.h>
#include <string.h>
#include <vfs_client.h>

static char g_paths[SHELL_MAX_PATHS][SHELL_PATH_LEN];
static int  g_path_count = 0;

void path_init(void) {
    path_clear();

    /* 默认搜索路径 */
    path_add("/sys/bin"); /* 系统工具 */
    path_add("/mnt/bin"); /* 用户工具 */
}

bool path_add(const char *dir) {
    if (!dir || !*dir) {
        return false;
    }

    if (g_path_count >= SHELL_MAX_PATHS) {
        return false;
    }

    size_t len = strlen(dir);
    if (len >= SHELL_PATH_LEN) {
        return false;
    }

    /* 检查是否已存在 */
    for (int i = 0; i < g_path_count; i++) {
        if (strcmp(g_paths[i], dir) == 0) {
            return true; /* 已存在,视为成功 */
        }
    }

    memcpy(g_paths[g_path_count], dir, len + 1);
    g_path_count++;
    return true;
}

void path_clear(void) {
    g_path_count = 0;
}

int path_count(void) {
    return g_path_count;
}

const char *path_get(int index) {
    if (index < 0 || index >= g_path_count) {
        return 0;
    }
    return g_paths[index];
}

/**
 * 检查文件是否存在且为普通文件
 */
static bool file_exists(const char *path) {
    struct vfs_stat st;
    int             ret = vfs_stat(path, &st);
    if (ret < 0) {
        return false;
    }
    return st.type == VFS_TYPE_FILE;
}

bool path_find(const char *name, char *out, size_t max_len) {
    if (!name || !*name || !out || max_len < 2) {
        return false;
    }

    /* 如果是绝对路径或相对路径,直接使用 */
    if (name[0] == '/' || (name[0] == '.' && name[1] == '/')) {
        size_t len = strlen(name);
        if (len >= max_len) {
            return false;
        }
        memcpy(out, name, len + 1);
        return file_exists(out);
    }

    /* 遍历 PATH 查找 */
    for (int i = 0; i < g_path_count; i++) {
        size_t dir_len  = strlen(g_paths[i]);
        size_t name_len = strlen(name);

        /* 尝试 path/name */
        if (dir_len + 1 + name_len < max_len) {
            memcpy(out, g_paths[i], dir_len);
            out[dir_len] = '/';
            memcpy(out + dir_len + 1, name, name_len + 1);

            if (file_exists(out)) {
                return true;
            }
        }

        /* 尝试 path/name.elf */
        if (dir_len + 1 + name_len + 4 < max_len) {
            memcpy(out, g_paths[i], dir_len);
            out[dir_len] = '/';
            strcpy(out + dir_len + 1, name);
            memcpy(out + dir_len + 1 + name_len, ".elf", 5);

            if (file_exists(out)) {
                return true;
            }
        }
    }

    return false;
}
