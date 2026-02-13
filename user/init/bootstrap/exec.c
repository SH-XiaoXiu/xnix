/**
 * @file exec.c
 * @brief 直接执行 ELF(绕过 VFS)- proc_image_builder 转发
 */

#include "bootstrap.h"

#include <xnix/proc.h>

int bootstrap_exec(const void *elf_data, size_t elf_size, const char *name, char **argv,
                   const struct spawn_handle *handles, int handle_count, const char *profile_name) {
    if (!elf_data || elf_size == 0) {
        return -1;
    }

    struct proc_image_builder b;
    proc_image_init(&b, name, elf_data, elf_size);

    if (profile_name) {
        proc_image_set_profile(&b, profile_name);
    }

    for (int i = 0; i < handle_count; i++) {
        proc_image_add_handle(&b, handles[i].src, handles[i].name);
    }

    if (argv) {
        for (int i = 0; argv[i]; i++) {
            proc_image_add_arg(&b, argv[i]);
        }
    }

    return proc_image_spawn(&b);
}
