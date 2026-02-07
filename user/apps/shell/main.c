/**
 * @file main.c
 * @brief Xnix Shell
 */

#include "path.h"
#include "unistd.h"

#include <d/protocol/tty.h>
#include <d/protocol/vfs.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/abi/process.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/termcolor.h>

#define MAX_LINE 256
#define MAX_ARGS 16

static handle_t g_tty_ep = HANDLE_INVALID;
static handle_t g_vfs_ep = HANDLE_INVALID;

/**
 * 通过 TTY IPC 设置前台进程
 */
static void shell_set_foreground(pid_t pid) {
    if (g_tty_ep == HANDLE_INVALID) {
        return;
    }
    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = TTY_OP_IOCTL;
    msg.regs.data[1] = TTY_IOCTL_SET_FOREGROUND;
    msg.regs.data[2] = (uint32_t)pid;

    sys_ipc_send(g_tty_ep, &msg, 100);
}

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
                                              {"run", cmd_run, "Run module by name"},
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
 * @param background 是否后台运行
 */
static void run_external(const char *path, int argc, char **argv, int background) {
    struct abi_exec_args exec_args;
    memset(&exec_args, 0, sizeof(exec_args));

    /* TODO: 应由 init 创建 app profile,shell 从配置或环境变量获取 */
    memcpy(exec_args.profile_name, "default", 8);

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

    /* 传递 handles */
    exec_args.handle_count = 0;
    if (g_vfs_ep != HANDLE_INVALID && exec_args.handle_count < ABI_EXEC_MAX_HANDLES) {
        exec_args.handles[exec_args.handle_count].src = g_vfs_ep;
        strncpy(exec_args.handles[exec_args.handle_count].name, "vfs_ep",
                sizeof(exec_args.handles[exec_args.handle_count].name) - 1);
        exec_args.handles[exec_args.handle_count]
            .name[sizeof(exec_args.handles[exec_args.handle_count].name) - 1] = '\0';
        exec_args.handle_count++;
    }
    if (g_tty_ep != HANDLE_INVALID && exec_args.handle_count < ABI_EXEC_MAX_HANDLES) {
        exec_args.handles[exec_args.handle_count].src = g_tty_ep;
        strncpy(exec_args.handles[exec_args.handle_count].name, "tty0",
                sizeof(exec_args.handles[exec_args.handle_count].name) - 1);
        exec_args.handles[exec_args.handle_count]
            .name[sizeof(exec_args.handles[exec_args.handle_count].name) - 1] = '\0';
        exec_args.handle_count++;
    }

    /* 执行 */
    int pid = sys_exec(&exec_args);
    if (pid > 0) {
        /* 复制CWD到子进程 */
        vfs_copy_cwd_to_child(pid);
    }
    if (pid < 0) {
        const char *err_msg;
        switch (pid) {
        case -1:
            err_msg = "permission denied (EPERM)";
            break;
        case -2:
            err_msg = "file not found (ENOENT)";
            break;
        case -12:
            err_msg = "out of memory (ENOMEM)";
            break;
        case -22:
            err_msg = "invalid executable (EINVAL)";
            break;
        default:
            err_msg = "unknown error";
            break;
        }
        printf("%s: %s (code %d)\n", argv[0], err_msg, pid);
        return;
    }

    if (background) {
        /* 后台运行,不等待 */
        printf("[%d] %s\n", pid, argv[0]);
        return;
    }

    /* 设置为前台进程 */
    shell_set_foreground(pid);

    /* 等待进程退出 */
    int status;
    int ret = sys_waitpid(pid, &status, 0);

    /* 清除前台进程 */
    shell_set_foreground(0);

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

    /* 检查是否后台运行(最后一个参数是 &) */
    int background = 0;
    if (argc > 0 && strcmp(argv[argc - 1], "&") == 0) {
        background = 1;
        argc--;
        argv[argc] = NULL;
    }

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
        run_external(path, argc, argv, background);
    } else {
        termcolor_set(stdout, TERM_COLOR_WHITE, TERM_COLOR_BLACK);
        printf("Command not found: %s\n", argv[0]);
        printf("Type 'help' for available commands.\n");
        termcolor_reset(stdout);
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
    (void)argc;
    (void)argv;

    /* sys_spawn has been removed - use exec from filesystem instead */
    printf("Error: 'run' command is deprecated (sys_spawn removed)\n");
    printf("Use regular commands to execute programs from /sys or /mnt\n");
    return;

#if 0 /* Old implementation using sys_spawn - kept for reference */
    if (argc < 2) {
        printf("Usage: run <module_name>\n");
        return;
    }

    struct spawn_args spawn;
    memset(&spawn, 0, sizeof(spawn));
    strncpy(spawn.name, argv[1], ABI_SPAWN_NAME_LEN - 1);
    strncpy(spawn.module_name, argv[1], ABI_SPAWN_NAME_LEN - 1);
    spawn.handle_count = 0;

    int pid = sys_spawn(&spawn);
    if (pid < 0) {
        printf("Failed to spawn module '%s' (error %d)\n", argv[1], pid);
        return;
    }

    /* 复制CWD到子进程 */
    vfs_copy_cwd_to_child(pid);

    printf("Started module '%s' (pid=%d)\n", argv[1], pid);

    shell_set_foreground(pid);

    int status;
    int ret = sys_waitpid(pid, &status, 0);

    shell_set_foreground(0);

    if (ret > 0) {
        printf("Process %d exited (status=%d)\n", pid, status);
    }
#endif
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

    int ret = vfs_chdir(path);
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
    int  ret = vfs_getcwd(cwd, sizeof(cwd));
    if (ret < 0) {
        printf("pwd: error %d\n", ret);
    } else {
        printf("%s\n", cwd);
    }
}

int main(int argc, char **argv) {
    char line[MAX_LINE];

    /* 解析命令行参数获取tty名称 */
    const char *tty_name = "tty0"; /* 默认tty0 */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--tty=", 6) == 0) {
            tty_name = argv[i] + 6;
            break;
        }
    }

    /* 查找 handles */
    g_tty_ep = env_get_handle(tty_name);
    g_vfs_ep = env_get_handle("vfs_ep");

    /* 重新绑定stdio到指定的tty */
    if (g_tty_ep != HANDLE_INVALID) {
        _stdio_set_tty(stdout, g_tty_ep);
        _stdio_set_tty(stderr, g_tty_ep);
        _stdio_set_tty(stdin, g_tty_ep);
    }

    /* 初始化 VFS 客户端 */
    vfs_client_init(g_vfs_ep);

    /* 初始化 PATH */
    path_init();

    svc_notify_ready("shell");

    /* 输出欢迎信息 */
    printf("\nXnix Shell\n");
    printf("Type 'help' for available commands.\n\n");

    while (1) {
        char cwd[256];
        int  cwd_ret = vfs_getcwd(cwd, sizeof(cwd));

        if (cwd_ret >= 0) {
            printf("%s> ", cwd);
            fflush(stdout);
        } else {
            printf("> ");
            fflush(stdout);
        }

        if (gets_s(line, sizeof(line)) == NULL) {
            msleep(100);
            continue;
        }

        /* 防止用户按住回车键时产生输入风暴 */
        if (line[0] == '\0') {
            msleep(20);
            continue;
        }

        execute_command(line);
    }

    return 0;
}
