/**
 * @file main.c
 * @brief 读取文件内容
 *
 * 用法:
 *   read <file>              读取整个文件
 *   read <file> -n <lines>   读取前 N 行
 *   read <file> -c <bytes>   读取前 N 字节
 */

#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

#define READ_BUF_SIZE 256

static int simple_atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

static void print_usage(void) {
    printf("Usage: read <file> [-n lines] [-c bytes]\n");
    printf("  -n <lines>  Read first N lines\n");
    printf("  -c <bytes>  Read first N bytes\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *path      = argv[1];
    int         max_lines = 0; /* 0 = 不限制 */
    int         max_bytes = 0; /* 0 = 不限制 */

    /* 解析参数 */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_lines = simple_atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            max_bytes = simple_atoi(argv[++i]);
        } else {
            print_usage();
            return 1;
        }
    }

    /* 打开文件 */
    int fd = sys_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        printf("read: cannot open '%s': error %d\n", path, fd);
        return 1;
    }

    /* 读取并输出 */
    char buf[READ_BUF_SIZE];
    int  total_bytes = 0;
    int  total_lines = 0;
    int  done        = 0;

    while (!done) {
        int to_read = sizeof(buf);

        /* 如果限制字节数,调整读取量 */
        if (max_bytes > 0 && total_bytes + to_read > max_bytes) {
            to_read = max_bytes - total_bytes;
            if (to_read <= 0) {
                break;
            }
        }

        int n = sys_read(fd, buf, to_read);
        if (n <= 0) {
            break;
        }

        /* 输出内容,检查行数限制 */
        for (int i = 0; i < n && !done; i++) {
            if (max_lines > 0 && buf[i] == '\n') {
                total_lines++;
                if (total_lines >= max_lines) {
                    /* 输出这个换行符后停止 */
                    putchar(buf[i]);
                    done = 1;
                    break;
                }
            }
            putchar(buf[i]);
        }

        total_bytes += n;

        /* 检查字节数限制 */
        if (max_bytes > 0 && total_bytes >= max_bytes) {
            break;
        }
    }

    /* 如果文件不以换行结尾,添加一个 */
    if (total_bytes > 0) {
        fflush(NULL);
    }

    sys_close(fd);
    return 0;
}
