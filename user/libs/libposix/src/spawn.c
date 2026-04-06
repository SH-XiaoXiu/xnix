#include <spawn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
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

static int __posix_spawn_resolve_path(const char *file, char *out, size_t out_len) {
    static const char *paths[] = {"/bin", "/sbin", "/mnt/bin", NULL};

    if (!file || !file[0] || !out || out_len < 2) {
        return -EINVAL;
    }

    if (file[0] == '/' || (file[0] == '.' && file[1] == '/')) {
        size_t len = strlen(file);
        if (len >= out_len) {
            return -ENAMETOOLONG;
        }
        memcpy(out, file, len + 1);
        return file_exists(out) ? 0 : -ENOENT;
    }

    for (int i = 0; paths[i]; i++) {
        int written = snprintf(out, out_len, "%s/%s", paths[i], file);
        if (written > 0 && (size_t)written < out_len && file_exists(out)) {
            return 0;
        }

        written = snprintf(out, out_len, "%s/%s.elf", paths[i], file);
        if (written > 0 && (size_t)written < out_len && file_exists(out)) {
            return 0;
        }
    }

    return -ENOENT;
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
    int  ret = __posix_spawn_resolve_path(file, path, sizeof(path));
    if (ret < 0) {
        return ret;
    }
    return posix_spawn(path, argc, argv);
}

int posix_spawn_make_exec_args(struct abi_exec_args *out, const char *path,
                               int argc, const char **argv) {
    if (!out || !path) {
        return -EINVAL;
    }

    struct proc_builder b;
    proc_new(&b, path);
    proc_inherit_named(&b);
    for (int i = 0; i < argc; i++) {
        proc_add_arg(&b, argv[i]);
    }

    memcpy(out, &b.args, sizeof(*out));
    return 0;
}

int posix_spawnp_make_exec_args(struct abi_exec_args *out, const char *file,
                                int argc, const char **argv) {
    char path[256];
    int  ret = __posix_spawn_resolve_path(file, path, sizeof(path));
    if (ret < 0) {
        return ret;
    }
    return posix_spawn_make_exec_args(out, path, argc, argv);
}
