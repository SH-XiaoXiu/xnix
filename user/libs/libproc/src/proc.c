/**
 * @file proc.c
 * @brief 进程创建 Builder 实现
 */

#include <string.h>
#include <xnix/abi/process.h>
#include <xnix/proc.h>
#include <xnix/syscall.h>

void proc_init(struct proc_builder *b, const char *path) {
    memset(&b->args, 0, sizeof(b->args));
    size_t len = strlen(path);
    if (len >= ABI_EXEC_PATH_MAX) {
        len = ABI_EXEC_PATH_MAX - 1;
    }
    memcpy(b->args.path, path, len);
    b->args.path[len] = '\0';
}

void proc_set_profile(struct proc_builder *b, const char *profile) {
    size_t len = strlen(profile);
    if (len >= ABI_SPAWN_PROFILE_LEN) {
        len = ABI_SPAWN_PROFILE_LEN - 1;
    }
    memcpy(b->args.profile_name, profile, len);
    b->args.profile_name[len] = '\0';
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

int proc_spawn(struct proc_builder *b) {
    return sys_exec(&b->args);
}
