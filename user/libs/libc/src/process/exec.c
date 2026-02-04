#include <d/protocol/vfs.h>
#include <stdlib.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/abi/process.h>
#include <xnix/errno.h>
#include <xnix/syscall.h>

static void derive_proc_name(char out[ABI_PROC_NAME_MAX], const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' && p[1]) {
            base = p + 1;
        }
    }

    size_t len = 0;
    while (base[len] && base[len] != '.' && len < ABI_PROC_NAME_MAX - 1) {
        len++;
    }

    if (len == 0) {
        out[0] = 'p';
        out[1] = 'r';
        out[2] = 'o';
        out[3] = 'c';
        out[4] = '\0';
        return;
    }

    memcpy(out, base, len);
    out[len] = '\0';
}

int sys_exec(struct abi_exec_args *args) {
    if (!args || args->path[0] != '/') {
        return -EINVAL;
    }

    struct vfs_stat st;
    int             ret = vfs_stat(args->path, &st);
    if (ret < 0) {
        return ret;
    }

    if (st.type != VFS_TYPE_FILE || st.size == 0) {
        return -EINVAL;
    }

    int fd = vfs_open(args->path, 0);
    if (fd < 0) {
        return fd;
    }

    void *elf = malloc(st.size);
    if (!elf) {
        vfs_close(fd);
        return -ENOMEM;
    }

    uint32_t total = 0;
    while (total < st.size) {
        ssize_t n = vfs_read(fd, (uint8_t *)elf + total, (size_t)(st.size - total));
        if (n < 0) {
            free(elf);
            vfs_close(fd);
            return (int)n;
        }
        if (n == 0) {
            free(elf);
            vfs_close(fd);
            return -EIO;
        }
        total += (uint32_t)n;
    }
    vfs_close(fd);

    struct abi_exec_image_args img_args;
    memset(&img_args, 0, sizeof(img_args));
    derive_proc_name(img_args.name, args->path);
    if (args->profile_name[0] != '\0') {
        size_t profile_len = strnlen(args->profile_name, ABI_SPAWN_PROFILE_LEN - 1);
        memcpy(img_args.profile_name, args->profile_name, profile_len);
        img_args.profile_name[profile_len] = '\0';
    }
    img_args.elf_ptr  = (uint32_t)(uintptr_t)elf;
    img_args.elf_size = st.size;
    img_args.flags    = args->flags;

    int argc = args->argc;
    if (argc < 0) {
        argc = 0;
    }
    if (argc > ABI_EXEC_MAX_ARGS) {
        argc = ABI_EXEC_MAX_ARGS;
    }
    img_args.argc = argc;
    memcpy(img_args.argv, args->argv, sizeof(img_args.argv));

    uint32_t handle_count = args->handle_count;
    if (handle_count > ABI_EXEC_MAX_HANDLES) {
        handle_count = ABI_EXEC_MAX_HANDLES;
    }
    img_args.handle_count = handle_count;
    memcpy(img_args.handles, args->handles, handle_count * sizeof(args->handles[0]));

    int pid = syscall1(SYS_EXEC, (uint32_t)(uintptr_t)&img_args);
    free(elf);
    return pid;
}
