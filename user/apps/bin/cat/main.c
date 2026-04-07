/**
 * @file main.c
 * @brief 显示文件内容
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/protocol/vfs.h>
#include <xnix/syscall.h>

#define READ_BUF_SIZE 256

static int cat_fd(int fd, const char *name, int is_vfs) {
    char buf[READ_BUF_SIZE];
    int  n;

    if (is_vfs) {
        while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
            for (int i = 0; i < n; i++)
                putchar(buf[i]);
        }
    } else {
        while ((n = (int)read(fd, buf, sizeof(buf))) > 0) {
            for (int i = 0; i < n; i++)
                putchar(buf[i]);
        }
    }

    if (n < 0) {
        printf("cat: %s: %s\n", name, strerror(is_vfs ? -n : errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    /* 无参数: 从 stdin 读 */
    if (argc < 2) {
        return cat_fd(STDIN_FILENO, "<stdin>", 0);
    }

    const char *path = argv[1];

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open '%s': %s\n", path, strerror(-fd));
        return 1;
    }

    int ret = cat_fd(fd, path, 1);
    vfs_close(fd);
    return ret;
}
