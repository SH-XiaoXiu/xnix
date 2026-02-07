/**
 * @file exec.c
 * @brief 直接执行 ELF(绕过 VFS)
 */

#include "bootstrap.h"

#include <stdio.h>
#include <string.h>
#include <xnix/abi/process.h>
#include <xnix/abi/syscall.h>

/* 直接调用系统调用 */
static inline int syscall1(int no, void *arg1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(no), "b"(arg1) : "memory");
    return ret;
}

int bootstrap_exec(const void *elf_data, size_t elf_size, const char *name, char **argv,
                   const struct spawn_handle *handles, int handle_count, const char *profile_name) {
    if (!elf_data || elf_size == 0) {
        return -1;
    }

    struct abi_exec_image_args exec_args;
    memset(&exec_args, 0, sizeof(exec_args));

    /* 设置 ELF 数据 */
    exec_args.elf_ptr  = (uint32_t)(uintptr_t)elf_data;
    exec_args.elf_size = (uint32_t)elf_size;

    /* 设置进程名(如果提供) */
    if (name) {
        size_t name_len = strlen(name);
        if (name_len >= ABI_PROC_NAME_MAX) {
            name_len = ABI_PROC_NAME_MAX - 1;
        }
        memcpy(exec_args.name, name, name_len);
        exec_args.name[name_len] = '\0';
    } else {
        exec_args.name[0] = '\0';
    }

    /* 设置权限 profile */
    if (profile_name) {
        size_t profile_len = strlen(profile_name);
        if (profile_len >= ABI_SPAWN_PROFILE_LEN) {
            profile_len = ABI_SPAWN_PROFILE_LEN - 1;
        }
        memcpy(exec_args.profile_name, profile_name, profile_len);
        exec_args.profile_name[profile_len] = '\0';
    } else {
        exec_args.profile_name[0] = '\0';
    }

    /* 解析参数 */
    exec_args.argc = 0;
    if (argv) {
        while (argv[exec_args.argc] && exec_args.argc < ABI_EXEC_MAX_ARGS) {
            size_t len = strlen(argv[exec_args.argc]);
            if (len >= ABI_EXEC_MAX_ARG_LEN) {
                len = ABI_EXEC_MAX_ARG_LEN - 1;
            }
            memcpy(exec_args.argv[exec_args.argc], argv[exec_args.argc], len);
            exec_args.argv[exec_args.argc][len] = '\0';
            exec_args.argc++;
        }
    }

    /* 传递 handles */
    exec_args.handle_count = 0;
    if (handles && handle_count > 0) {
        int max = handle_count < ABI_EXEC_MAX_HANDLES ? handle_count : ABI_EXEC_MAX_HANDLES;
        for (int i = 0; i < max; i++) {
            exec_args.handles[i] = handles[i];
        }
        exec_args.handle_count = (uint32_t)max;
    }

    exec_args.flags = 0;

    /* 直接调用 SYS_EXEC */
    return syscall1(SYS_EXEC, &exec_args);
}
