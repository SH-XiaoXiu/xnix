/**
 * @file main.c
 * @brief 显示文件内容
 */

#include <d/protocol/vfs.h>
#include <errno.h>
#include <stdio.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

#define READ_BUF_SIZE 256

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }

    const char *path = argv[1];

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open '%s': %s\n", path, strerror(-fd));
        return 1;
    }

    char buf[READ_BUF_SIZE];
    int  n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        /* 输出读取的字节 */
        for (int i = 0; i < n; i++) {
            putchar(buf[i]);
        }
    }

    if (n < 0) {
        printf("cat: read error: %s\n", strerror(-n));
        vfs_close(fd);
        return 1;
    }

    vfs_close(fd);
    return 0;
}
