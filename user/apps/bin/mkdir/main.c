/**
 * @file main.c
 * @brief 创建目录
 */

#include <stdio.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: mkdir <path>\n");
        return 1;
    }

    const char *path = argv[1];

    int ret = vfs_mkdir(path);
    if (ret < 0) {
        printf("mkdir: cannot create '%s': error %d\n", path, ret);
        return 1;
    }

    return 0;
}
