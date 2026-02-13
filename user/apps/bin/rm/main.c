/**
 * @file main.c
 * @brief 删除文件或目录
 */

#include <errno.h>
#include <stdio.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: rm <path>\n");
        return 1;
    }

    const char *path = argv[1];

    int ret = vfs_delete(path);
    if (ret < 0) {
        printf("rm: cannot remove '%s': %s\n", path, strerror(-ret));
        return 1;
    }

    return 0;
}
