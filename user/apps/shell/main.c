/**
 * @file main.c
 * @brief Xnix Shell
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

#define MAX_LINE 256

/* 模块索引约定 */
#define MODULE_DEMO_BASE 4

/* 内置命令 */
static void cmd_help(void);
static void cmd_echo(const char *args);
static void cmd_clear(void);
static void cmd_run(const char *args);
static void cmd_kill(const char *args);
static void cmd_ls(const char *args);
static void cmd_cat(const char *args);
static void cmd_write(const char *args);
static void cmd_mkdir(const char *args);
static void cmd_rm(const char *args);

static void execute_command(char *line) {
    /* 跳过前导空格 */
    while (*line == ' ') {
        line++;
    }

    if (*line == '\0') {
        return;
    }

    /* 分离命令和参数 */
    char *cmd  = line;
    char *args = NULL;

    char *space = strchr(line, ' ');
    if (space) {
        *space = '\0';
        args   = space + 1;
        /* 跳过参数前导空格 */
        while (*args == ' ') {
            args++;
        }
    }

    /* 匹配命令 */
    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "echo") == 0) {
        cmd_echo(args);
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(cmd, "run") == 0) {
        cmd_run(args);
    } else if (strcmp(cmd, "kill") == 0) {
        cmd_kill(args);
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls(args);
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(args);
    } else if (strcmp(cmd, "write") == 0) {
        cmd_write(args);
    } else if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(args);
    } else if (strcmp(cmd, "rm") == 0) {
        cmd_rm(args);
    } else {
        printf("Unknown command: %s\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

static void cmd_help(void) {
    printf("Available commands:\n");
    printf("  help             - Show this help\n");
    printf("  echo <text>      - Echo text\n");
    printf("  clear            - Clear screen\n");
    printf("  run <index>      - Run module (index starts from %d)\n", MODULE_DEMO_BASE);
    printf("  kill <pid>       - Terminate process\n");
    printf("  ls [path]        - List directory\n");
    printf("  cat <file>       - Display file contents\n");
    printf("  write <file> <t> - Write text to file\n");
    printf("  mkdir <path>     - Create directory\n");
    printf("  rm <path>        - Delete file or empty directory\n");
}

static void cmd_echo(const char *args) {
    if (args && *args) {
        printf("%s\n", args);
    } else {
        printf("\n");
    }
}

static void cmd_clear(void) {
    /* ANSI 清屏序列 */
    printf("\033[2J\033[H");
}

/* 简单的 atoi */
static int simple_atoi(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return n;
}

static void cmd_run(const char *args) {
    if (!args || !*args) {
        printf("Usage: run <module_index>\n");
        printf("  Module indices start from %d for demos\n", MODULE_DEMO_BASE);
        return;
    }

    int index = simple_atoi(args);
    if (index < MODULE_DEMO_BASE) {
        printf("Error: module index must be >= %d\n", MODULE_DEMO_BASE);
        return;
    }

    /* 构造进程名 */
    char name[16];
    snprintf(name, sizeof(name), "mod%d", index);

    struct spawn_args spawn = {0};
    for (int i = 0; name[i] && i < 15; i++) {
        spawn.name[i] = name[i];
    }
    spawn.module_index = (uint32_t)index;
    spawn.cap_count    = 0;

    int pid = sys_spawn(&spawn);
    if (pid < 0) {
        printf("Failed to spawn module %d (error %d)\n", index, pid);
        return;
    }

    printf("Started module %d (pid=%d)\n", index, pid);

    /* 设置为前台进程 */
    sys_set_foreground(pid);

    /* 等待进程退出 */
    int status;
    int ret = sys_waitpid(pid, &status, 0);

    /* 清除前台进程 */
    sys_set_foreground(0);

    if (ret > 0) {
        printf("Process %d exited (status=%d)\n", pid, status);
    }
}

static void cmd_kill(const char *args) {
    if (!args || !*args) {
        printf("Usage: kill <pid>\n");
        return;
    }

    int pid = simple_atoi(args);
    if (pid <= 1) {
        printf("Error: cannot kill pid %d\n", pid);
        return;
    }

    int ret = sys_kill(pid, SIGTERM);
    if (ret < 0) {
        printf("Failed to kill pid %d (error %d)\n", pid, ret);
    } else {
        printf("Sent SIGTERM to pid %d\n", pid);
    }
}

static void cmd_ls(const char *args) {
    const char *path = (args && *args) ? args : "/";

    int fd = sys_opendir(path);
    if (fd < 0) {
        printf("ls: cannot open '%s': error %d\n", path, fd);
        return;
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
}

static void cmd_cat(const char *args) {
    if (!args || !*args) {
        printf("Usage: cat <file>\n");
        return;
    }

    int fd = sys_open(args, VFS_O_RDONLY);
    if (fd < 0) {
        printf("cat: cannot open '%s': error %d\n", args, fd);
        return;
    }

    char buf[256];
    int  n;
    while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    printf("\n"); /* 确保输出后换行 */

    if (n < 0) {
        printf("cat: read error: %d\n", n);
    }

    sys_close(fd);
}

static void cmd_write(const char *args) {
    if (!args || !*args) {
        printf("Usage: write <file> <text>\n");
        return;
    }

    /* 分离文件名和内容 */
    const char *file = args;
    const char *text = strchr(args, ' ');
    if (!text) {
        printf("Usage: write <file> <text>\n");
        return;
    }

    /* 复制文件名 */
    char path[128];
    int  len = text - file;
    if (len >= (int)sizeof(path)) {
        len = sizeof(path) - 1;
    }
    memcpy(path, file, len);
    path[len] = '\0';

    text++; /* 跳过空格 */

    int fd = sys_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) {
        printf("write: cannot open '%s': error %d\n", path, fd);
        return;
    }

    int text_len = strlen(text);
    int n        = sys_write2(fd, text, text_len);
    if (n < 0) {
        printf("write: write error: %d\n", n);
    } else {
        printf("Wrote %d bytes to %s\n", n, path);
    }

    sys_close(fd);
}

static void cmd_mkdir(const char *args) {
    if (!args || !*args) {
        printf("Usage: mkdir <path>\n");
        return;
    }

    int ret = sys_mkdir(args);
    if (ret < 0) {
        printf("mkdir: cannot create '%s': error %d\n", args, ret);
    }
}

static void cmd_rm(const char *args) {
    if (!args || !*args) {
        printf("Usage: rm <path>\n");
        return;
    }

    int ret = sys_del(args);
    if (ret < 0) {
        printf("rm: cannot remove '%s': error %d\n", args, ret);
    }
}

int main(void) {
    char line[MAX_LINE];

    printf("\n");
    printf("Xnix Shell\n");
    printf("Type 'help' for available commands.\n");
    printf("\n");

    while (1) {
        printf("> ");
        fflush(NULL);

        if (gets_s(line, sizeof(line)) == NULL) {
            continue;
        }

        execute_command(line);
    }

    return 0;
}
