/**
 * @file main.c
 * @brief 显示文件内容
 */

#include <stdio.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }

    const char *path = argv[1];

    int fd = sys_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open '%s': error %d\n", path, fd);
        return 1;
    }

    char buf[256];
    int  n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        sys_write(1, buf, n); /* 直接输出原始字节 */
    }

    if (n < 0) {
        printf("cat: read error: %d\n", n);
        sys_close(fd);
        return 1;
    }

    sys_close(fd);
    return 0;
}
