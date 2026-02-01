/**
 * @file main.c
 * @brief 列出目录内容
 */

#include <stdio.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

int main(int argc, char **argv) {
    const char *path = "/";

    if (argc > 1) {
        path = argv[1];
    }

    int fd = sys_opendir(path);
    if (fd < 0) {
        printf("ls: cannot open '%s': error %d\n", path, fd);
        return 1;
    }

    struct vfs_dirent entry;
    uint32_t          index = 0;

    while (sys_readdir(fd, index, &entry) == 0) {
        const char *type_str = (entry.type == VFS_TYPE_DIR) ? "d" : "-";
        printf("%s %s\n", type_str, entry.name);
        index++;
    }

    if (index == 0) {
        printf("(empty)\n");
    }

    sys_close(fd);
    return 0;
}
