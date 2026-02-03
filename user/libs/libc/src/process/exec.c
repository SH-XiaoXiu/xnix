#include <d/protocol/vfs.h>
#include <stdio.h>
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

    // printf("[exec] Checking %s\n", args->path);
    struct vfs_stat st;
    int             ret = vfs_stat(args->path, &st);
    if (ret < 0) {
        // printf("[exec] stat failed: %d\n", ret);
        return ret;
    }
    // printf("[exec] File size: %u bytes\n", st.size);

    if (st.type != VFS_TYPE_FILE || st.size == 0) {
        return -EINVAL;
    }

    // printf("[exec] Opening file...\n");
    int fd = vfs_open(args->path, 0);
    if (fd < 0) {
        // printf("[exec] open failed: %d\n", fd);
        return fd;
    }
    // printf("[exec] File opened, fd=%d\n", fd);

    void *elf = malloc(st.size);
    if (!elf) {
        vfs_close(fd);
        return -ENOMEM;
    }

    // printf("[exec] Reading %u bytes...\n", st.size);
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
    // printf("[exec] Read complete (%u bytes)\n", total);

    struct abi_exec_image_args img_args;
    memset(&img_args, 0, sizeof(img_args));
    derive_proc_name(img_args.name, args->path);
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

    uint32_t cap_count = args->cap_count;
    if (cap_count > ABI_EXEC_MAX_CAPS) {
        cap_count = ABI_EXEC_MAX_CAPS;
    }
    img_args.cap_count = cap_count;
    memcpy(img_args.caps, args->caps, cap_count * sizeof(args->caps[0]));

    // printf("[exec] Calling kernel syscall...\n");
    int pid = syscall1(SYS_EXEC, (uint32_t)(uintptr_t)&img_args);
    free(elf);
    // printf("[exec] Syscall returned pid=%d\n", pid);
    return pid;
}
