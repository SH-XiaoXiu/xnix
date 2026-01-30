/**
 * @file main.c
 * @brief Xnix Shell
 */

#include <stdio.h>
#include <string.h>

#define MAX_LINE 256

/* 内置命令 */
static void cmd_help(void);
static void cmd_echo(const char *args);
static void cmd_clear(void);

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
    } else {
        printf("Unknown command: %s\n", cmd);
        printf("Type 'help' for available commands.\n");
    }
}

static void cmd_help(void) {
    printf("Available commands:\n");
    printf("  help   - Show this help\n");
    printf("  echo   - Echo text\n");
    printf("  clear  - Clear screen\n");
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
