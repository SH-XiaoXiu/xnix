/**
 * @file proc.c
 * @brief 进程创建 Builder 实现
 */

#include <string.h>
#include <xnix/abi/process.h>
#include <xnix/abi/syscall.h>
#include <xnix/proc.h>
#include <xnix/syscall.h>

/* ===== VFS 路径 builder ===== */

void proc_init(struct proc_builder *b, const char *path) {
    memset(&b->args, 0, sizeof(b->args));
    size_t len = strlen(path);
    if (len >= ABI_EXEC_PATH_MAX) {
        len = ABI_EXEC_PATH_MAX - 1;
    }
    memcpy(b->args.path, path, len);
    b->args.path[len] = '\0';
}

void proc_new(struct proc_builder *b, const char *path) {
    proc_init(b, path);
    proc_inherit_stdio(b);
}

void proc_set_profile(struct proc_builder *b, const char *profile) {
    size_t len = strlen(profile);
    if (len >= ABI_SPAWN_PROFILE_LEN) {
        len = ABI_SPAWN_PROFILE_LEN - 1;
    }
    memcpy(b->args.profile_name, profile, len);
    b->args.profile_name[len] = '\0';
}

void proc_set_flags(struct proc_builder *b, uint32_t flags) {
    b->args.flags = flags;
}

void proc_inherit_stdio(struct proc_builder *b) {
    b->args.flags |= ABI_EXEC_INHERIT_STDIO;
}

void proc_inherit_named(struct proc_builder *b) {
    b->args.flags |= ABI_EXEC_INHERIT_NAMED;
}

void proc_inherit_all(struct proc_builder *b) {
    b->args.flags |= ABI_EXEC_INHERIT_ALL;
}

void proc_inherit_perm(struct proc_builder *b) {
    b->args.flags |= ABI_EXEC_INHERIT_PERM;
}

void proc_add_handle(struct proc_builder *b, handle_t src, const char *name) {
    if (b->args.handle_count >= ABI_EXEC_MAX_HANDLES) {
        return;
    }
    uint32_t i             = b->args.handle_count;
    b->args.handles[i].src = src;
    size_t len             = strlen(name);
    if (len >= HANDLE_NAME_MAX) {
        len = HANDLE_NAME_MAX - 1;
    }
    memcpy(b->args.handles[i].name, name, len);
    b->args.handles[i].name[len] = '\0';
    b->args.handle_count++;
}

void proc_add_arg(struct proc_builder *b, const char *arg) {
    if (b->args.argc >= ABI_EXEC_MAX_ARGS) {
        return;
    }
    int    idx = b->args.argc;
    size_t len = strlen(arg);
    if (len >= ABI_EXEC_MAX_ARG_LEN) {
        len = ABI_EXEC_MAX_ARG_LEN - 1;
    }
    memcpy(b->args.argv[idx], arg, len);
    b->args.argv[idx][len] = '\0';
    b->args.argc++;
}

void proc_add_args_string(struct proc_builder *b, const char *args_str) {
    if (!args_str || args_str[0] == '\0') {
        return;
    }

    const char *p = args_str;
    while (*p) {
        /* 跳过空白 */
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        /* 提取一个 token */
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }

        size_t len = (size_t)(p - start);
        if (len == 0) {
            continue;
        }

        if (b->args.argc >= ABI_EXEC_MAX_ARGS) {
            break;
        }
        if (len >= ABI_EXEC_MAX_ARG_LEN) {
            len = ABI_EXEC_MAX_ARG_LEN - 1;
        }
        int idx = b->args.argc;
        memcpy(b->args.argv[idx], start, len);
        b->args.argv[idx][len] = '\0';
        b->args.argc++;
    }
}

int proc_spawn(struct proc_builder *b) {
    return sys_exec(&b->args);
}

int proc_spawn_simple(const char *path) {
    struct proc_builder b;
    proc_new(&b, path);
    proc_inherit_named(&b);
    return proc_spawn(&b);
}

int proc_spawn_args(const char *path, int argc, const char **argv) {
    struct proc_builder b;
    proc_new(&b, path);
    proc_inherit_named(&b);
    for (int i = 0; i < argc; i++) {
        proc_add_arg(&b, argv[i]);
    }
    return proc_spawn(&b);
}

/* ===== 内存 ELF builder ===== */

void proc_image_init(struct proc_image_builder *b, const char *name,
                     const void *elf_data, size_t elf_size) {
    memset(&b->args, 0, sizeof(b->args));
    b->args.elf_ptr  = (uint32_t)(uintptr_t)elf_data;
    b->args.elf_size = (uint32_t)elf_size;

    if (name) {
        size_t len = strlen(name);
        if (len >= ABI_PROC_NAME_MAX) {
            len = ABI_PROC_NAME_MAX - 1;
        }
        memcpy(b->args.name, name, len);
        b->args.name[len] = '\0';
    }
}

void proc_image_set_profile(struct proc_image_builder *b, const char *profile) {
    size_t len = strlen(profile);
    if (len >= ABI_SPAWN_PROFILE_LEN) {
        len = ABI_SPAWN_PROFILE_LEN - 1;
    }
    memcpy(b->args.profile_name, profile, len);
    b->args.profile_name[len] = '\0';
}

void proc_image_set_flags(struct proc_image_builder *b, uint32_t flags) {
    b->args.flags = flags;
}

void proc_image_add_handle(struct proc_image_builder *b, handle_t src,
                           const char *name) {
    if (b->args.handle_count >= ABI_EXEC_MAX_HANDLES) {
        return;
    }
    uint32_t i             = b->args.handle_count;
    b->args.handles[i].src = src;
    size_t len             = strlen(name);
    if (len >= HANDLE_NAME_MAX) {
        len = HANDLE_NAME_MAX - 1;
    }
    memcpy(b->args.handles[i].name, name, len);
    b->args.handles[i].name[len] = '\0';
    b->args.handle_count++;
}

void proc_image_add_arg(struct proc_image_builder *b, const char *arg) {
    if (b->args.argc >= ABI_EXEC_MAX_ARGS) {
        return;
    }
    int    idx = b->args.argc;
    size_t len = strlen(arg);
    if (len >= ABI_EXEC_MAX_ARG_LEN) {
        len = ABI_EXEC_MAX_ARG_LEN - 1;
    }
    memcpy(b->args.argv[idx], arg, len);
    b->args.argv[idx][len] = '\0';
    b->args.argc++;
}

int proc_image_spawn(struct proc_image_builder *b) {
    return syscall1(SYS_EXEC, (uint32_t)(uintptr_t)&b->args);
}
