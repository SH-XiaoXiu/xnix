/**
 * @file main.c
 * @brief 写入文件内容
 *
 * 用法:
 *   write <file> <content>        覆盖写入
 *   write <file> -a <content>     追加写入
 *   write <file>                  从标准输入读取(Ctrl+D 结束)
 *   write <file> -a               从标准输入追加
 */

#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

static void print_usage(void) {
    printf("Usage: write <file> [-a] [content]\n");
    printf("  -a          Append mode (default: overwrite)\n");
    printf("  content     Text to write (if omitted, read from stdin)\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *path = argv[1];
    int append_mode = 0;
    const char *content = NULL;
    int content_start = 2;

    /* 解析参数 */
    if (argc > 2 && strcmp(argv[2], "-a") == 0) {
        append_mode = 1;
        content_start = 3;
    }

    /* 收集内容参数 */
    if (argc > content_start) {
        content = argv[content_start];
    }

    /* 确定打开标志 */
    uint32_t flags = VFS_O_WRONLY | VFS_O_CREAT;
    if (append_mode) {
        flags |= VFS_O_APPEND;
    } else {
        flags |= VFS_O_TRUNC;
    }

    /* 打开文件 */
    int fd = sys_open(path, flags);
    if (fd < 0) {
        printf("write: cannot open '%s': error %d\n", path, fd);
        return 1;
    }

    int total_written = 0;

    if (content) {
        /* 写入命令行参数中的内容 */
        /* 合并所有参数 */
        for (int i = content_start; i < argc; i++) {
            if (i > content_start) {
                /* 参数之间加空格 */
                int ret = sys_write2(fd, " ", 1);
                if (ret < 0) {
                    printf("write: error writing: %d\n", ret);
                    sys_close(fd);
                    return 1;
                }
                total_written += ret;
            }

            size_t len = strlen(argv[i]);
            int ret = sys_write2(fd, argv[i], len);
            if (ret < 0) {
                printf("write: error writing: %d\n", ret);
                sys_close(fd);
                return 1;
            }
            total_written += ret;
        }

        /* 添加换行符 */
        int ret = sys_write2(fd, "\n", 1);
        if (ret > 0) {
            total_written += ret;
        }
    } else {
        /* 从标准输入读取 */
        printf("Enter text (Ctrl+D to finish):\n");

        char buf[256];
        int pos = 0;

        while (1) {
            int c = getchar();
            if (c < 0 || c == 4) {  /* EOF 或 Ctrl+D */
                /* 写入剩余缓冲区 */
                if (pos > 0) {
                    int ret = sys_write2(fd, buf, pos);
                    if (ret > 0) {
                        total_written += ret;
                    }
                }
                break;
            }

            buf[pos++] = (char)c;

            /* 缓冲区满或遇到换行,写入 */
            if (pos >= (int)sizeof(buf) - 1 || c == '\n') {
                int ret = sys_write2(fd, buf, pos);
                if (ret < 0) {
                    printf("\nwrite: error writing: %d\n", ret);
                    sys_close(fd);
                    return 1;
                }
                total_written += ret;
                pos = 0;
            }
        }
        printf("\n");
    }

    sys_close(fd);
    printf("Wrote %d bytes to %s\n", total_written, path);
    return 0;
}
