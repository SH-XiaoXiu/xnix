/**
 * @file main.c
 * @brief Xnix Shell
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>

#define MAX_LINE 256

/* 模块索引约定 */
#define MODULE_DEMO_BASE 4

/* 内置命令 */
static void cmd_help(void);
static void cmd_echo(const char *args);
static void cmd_clear(void);
static void cmd_run(const char *args);
static void cmd_kill(const char *args);

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
    } else {
        printf("Unknown command: %s\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

static void cmd_help(void) {
    printf("Available commands:\n");
    printf("  help        - Show this help\n");
    printf("  echo <text> - Echo text\n");
    printf("  clear       - Clear screen\n");
    printf("  run <index> - Run module (index starts from %d)\n", MODULE_DEMO_BASE);
    printf("  kill <pid>  - Terminate process\n");
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
