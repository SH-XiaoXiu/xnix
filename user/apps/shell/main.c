/**
 * @file main.c
 * @brief Xnix Shell
 */

#include "path.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/process.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

#define MAX_LINE 256
#define MAX_ARGS 16

/* 内置命令 */
static void cmd_help(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_run(int argc, char **argv);
static void cmd_kill(int argc, char **argv);
static void cmd_path(int argc, char **argv);
static void cmd_cd(int argc, char **argv);
static void cmd_pwd(int argc, char **argv);

/* 内置命令表 */
struct builtin_cmd {
    const char *name;
    void (*handler)(int argc, char **argv);
    const char *help;
};

static const struct builtin_cmd builtins[] = {{"help", cmd_help, "Show available commands"},
                                              {"echo", cmd_echo, "Echo text"},
                                              {"clear", cmd_clear, "Clear screen"},
                                              {"run", cmd_run, "Run module by index"},
                                              {"kill", cmd_kill, "Terminate process"},
                                              {"path", cmd_path, "Manage PATH"},
                                              {"cd", cmd_cd, "Change directory"},
                                              {"pwd", cmd_pwd, "Print working directory"},
                                              {"exit", NULL, "Exit shell"},
                                              {NULL, NULL, NULL}};

/* 简单的 atoi */
static int simple_atoi(const char *s) {
    int n   = 0;
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

/**
 * 解析命令行为 argc/argv
 */
static int parse_cmdline(char *line, char **argv, int max_args) {
    int argc = 0;

    while (*line && argc < max_args - 1) {
        /* 跳过空格 */
        while (*line == ' ' || *line == '\t') {
            line++;
        }

        if (!*line) {
            break;
        }

        /* 记录参数开始 */
        argv[argc++] = line;

        /* 找到参数结束 */
        while (*line && *line != ' ' && *line != '\t') {
            line++;
        }

        if (*line) {
            *line++ = '\0';
        }
    }

    argv[argc] = NULL;
    return argc;
}

/**
 * 查找内置命令
 */
static const struct builtin_cmd *find_builtin(const char *name) {
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            return &builtins[i];
        }
    }
    return NULL;
}

/**
 * 执行外部命令
 */
static void run_external(const char *path, int argc, char **argv) {
    struct abi_exec_args exec_args;
    memset(&exec_args, 0, sizeof(exec_args));

    /* 复制路径 */
    size_t path_len = strlen(path);
    if (path_len >= ABI_EXEC_PATH_MAX) {
        path_len = ABI_EXEC_PATH_MAX - 1;
    }
    memcpy(exec_args.path, path, path_len);
    exec_args.path[path_len] = '\0';

    /* 复制参数 */
    exec_args.argc = argc;
    if (exec_args.argc > ABI_EXEC_MAX_ARGS) {
        exec_args.argc = ABI_EXEC_MAX_ARGS;
    }

    for (int i = 0; i < exec_args.argc; i++) {
        size_t len = strlen(argv[i]);
        if (len >= ABI_EXEC_MAX_ARG_LEN) {
            len = ABI_EXEC_MAX_ARG_LEN - 1;
        }
        memcpy(exec_args.argv[i], argv[i], len);
        exec_args.argv[i][len] = '\0';
    }

    /* 执行 */
    int pid = sys_exec(&exec_args);
    if (pid < 0) {
        const char *err_msg;
        switch (pid) {
        case -2:
            err_msg = "file not found";
            break;
        case -12:
            err_msg = "out of memory";
            break;
        case -22:
            err_msg = "invalid executable";
            break;
        default:
            err_msg = "unknown error";
            break;
        }
        printf("%s: %s\n", argv[0], err_msg);
        return;
    }

    /* 设置为前台进程 */
    sys_set_foreground(pid);

    /* 等待进程退出 */
    int status;
    int ret = sys_waitpid(pid, &status, 0);

    /* 清除前台进程 */
    sys_set_foreground(0);

    if (ret > 0 && status != 0) {
        printf("Process %d exited with status %d\n", pid, status);
    }
}

static void execute_command(char *line) {
    char *argv[MAX_ARGS];
    int   argc = parse_cmdline(line, argv, MAX_ARGS);

    if (argc == 0) {
        return;
    }

    /* 检查内置命令 */
    const struct builtin_cmd *cmd = find_builtin(argv[0]);
    if (cmd) {
        if (cmd->handler) {
            cmd->handler(argc, argv);
        } else if (strcmp(cmd->name, "exit") == 0) {
            printf("Use Ctrl+D or close terminal to exit.\n");
        }
        return;
    }

    /* 外部命令:PATH 搜索 */
    char path[256];
    if (path_find(argv[0], path, sizeof(path))) {
        run_external(path, argc, argv);
    } else {
        printf("Command not found: %s\n", argv[0]);
        printf("Type 'help' for available commands.\n");
    }
}

static void cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("Built-in commands:\n");
    for (int i = 0; builtins[i].name; i++) {
        printf("  %-10s - %s\n", builtins[i].name, builtins[i].help);
    }
    printf("\nExternal commands are searched in PATH.\n");
    printf("Use 'path' to view/modify PATH.\n");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            printf(" ");
        }
        printf("%s", argv[i]);
    }
    printf("\n");
}

static void cmd_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("\033[2J\033[H");
}

static void cmd_run(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: run <module_index>\n");
        return;
    }

    int index = simple_atoi(argv[1]);
    if (index < 4) {
        printf("Error: module index must be >= 4\n");
        return;
    }

    char name[16];
    snprintf(name, sizeof(name), "mod%d", index);

    struct spawn_args spawn;
    memset(&spawn, 0, sizeof(spawn));
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

    sys_set_foreground(pid);

    int status;
    int ret = sys_waitpid(pid, &status, 0);

    sys_set_foreground(0);

    if (ret > 0) {
        printf("Process %d exited (status=%d)\n", pid, status);
    }
}

static void cmd_kill(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: kill <pid>\n");
        return;
    }

    int pid = simple_atoi(argv[1]);
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

static void cmd_path(int argc, char **argv) {
    if (argc < 2) {
        /* 显示当前 PATH */
        int count = path_count();
        if (count == 0) {
            printf("PATH is empty\n");
        } else {
            printf("PATH:\n");
            for (int i = 0; i < count; i++) {
                printf("  %s\n", path_get(i));
            }
        }
        return;
    }

    if (strcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            printf("Usage: path add <directory>\n");
            return;
        }
        if (path_add(argv[2])) {
            printf("Added: %s\n", argv[2]);
        } else {
            printf("Failed to add path\n");
        }
    } else if (strcmp(argv[1], "clear") == 0) {
        path_clear();
        printf("PATH cleared\n");
    } else if (strcmp(argv[1], "reset") == 0) {
        path_init();
        printf("PATH reset to default\n");
    } else {
        printf("Usage: path [add <dir> | clear | reset]\n");
    }
}

static void cmd_cd(int argc, char **argv) {
    const char *path = "/";
    if (argc > 1) {
        path = argv[1];
    }

    int ret = sys_chdir(path);
    if (ret < 0) {
        const char *err_msg;
        switch (ret) {
        case -2:
            err_msg = "No such directory";
            break;
        case -5:
            err_msg = "I/O error";
            break;
        case -20:
            err_msg = "Not a directory";
            break;
        case -36:
            err_msg = "Path too long";
            break;
        case -110:
            err_msg = "Connection timed out";
            break;
        default:
            printf("cd: %s: error %d\n", path, ret);
            return;
        }
        printf("cd: %s: %s\n", path, err_msg);
    }
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char cwd[256];
    int  ret = sys_getcwd(cwd, sizeof(cwd));
    if (ret < 0) {
        printf("pwd: error %d\n", ret);
    } else {
        printf("%s\n", cwd);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char line[MAX_LINE];

    /* 初始化 PATH */
    path_init();

    printf("\n");
    printf("Xnix Shell\n");
    printf("Type 'help' for available commands.\n");
    printf("\n");

    while (1) {
        char cwd[256];
        if (sys_getcwd(cwd, sizeof(cwd)) >= 0) {
            printf("%s> ", cwd);
        } else {
            printf("> ");
        }
        fflush(NULL);

        if (gets_s(line, sizeof(line)) == NULL) {
            continue;
        }

        execute_command(line);
    }

    return 0;
}
