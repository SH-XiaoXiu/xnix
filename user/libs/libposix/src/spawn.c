#include <spawn.h>
#include <stdbool.h>
#include <string.h>
#include <xnix/errno.h>
#include <vfs_client.h>
#include <xnix/protocol/vfs.h>
#include <xnix/proc.h>

static bool file_exists(const char *path) {
    struct vfs_stat st;
    int             ret = vfs_stat(path, &st);
    if (ret < 0) {
        return false;
    }
    return st.type == VFS_TYPE_FILE;
}

static bool resolve_spawn_path(const char *name, char *out, size_t out_len) {
    static const char *paths[] = {"/bin", "/sbin", "/mnt/bin", NULL};

    if (!name || !name[0] || !out || out_len < 2) {
        return false;
    }

    if (name[0] == '/' || (name[0] == '.' && name[1] == '/')) {
        size_t len = strlen(name);
        if (len >= out_len) {
            return false;
        }
        memcpy(out, name, len + 1);
        return file_exists(out);
    }

    for (int i = 0; paths[i]; i++) {
        size_t dir_len  = strlen(paths[i]);
        size_t name_len = strlen(name);

        if (dir_len + 1 + name_len < out_len) {
            memcpy(out, paths[i], dir_len);
            out[dir_len] = '/';
            memcpy(out + dir_len + 1, name, name_len + 1);
            if (file_exists(out)) {
                return true;
            }
        }

        if (dir_len + 1 + name_len + 4 < out_len) {
            memcpy(out, paths[i], dir_len);
            out[dir_len] = '/';
            memcpy(out + dir_len + 1, name, name_len);
            memcpy(out + dir_len + 1 + name_len, ".elf", 5);
            if (file_exists(out)) {
                return true;
            }
        }
    }

    return false;
}

int posix_spawn(const char *path, int argc, const char **argv) {
    int pid = proc_spawn_args(path, argc, argv);
    if (pid > 0) {
        vfs_copy_cwd_to_child(pid);
    }
    return pid;
}

int posix_spawnp(const char *file, int argc, const char **argv) {
    char path[256];
    if (!resolve_spawn_path(file, path, sizeof(path))) {
        return -ENOENT;
    }
    return posix_spawn(path, argc, argv);
}
