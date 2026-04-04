/**
 * @file main.c
 * @brief Xnix Shell
 */

#include "path.h"

#include <xnix/protocol/tty.h>
#include <xnix/protocol/vfs.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/proc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/termcolor.h>

#define MAX_LINE 256
#define MAX_ARGS 16

static handle_t g_tty_ep = HANDLE_INVALID;
static handle_t g_vfs_ep = HANDLE_INVALID;

/* ============== Job Table ============== */

#define MAX_JOBS 16

enum job_state {
    JOB_FREE    = 0,
    JOB_RUNNING = 1,
    JOB_STOPPED = 2,
    JOB_DONE    = 3,
};

struct job {
    int           state;
    pid_t         pid;
    char          cmd[64];
};

static struct job g_jobs[MAX_JOBS];

static int job_add(pid_t pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_FREE || g_jobs[i].state == JOB_DONE) {
            g_jobs[i].state = JOB_RUNNING;
            g_jobs[i].pid   = pid;
            strncpy(g_jobs[i].cmd, cmd, sizeof(g_jobs[i].cmd) - 1);
            g_jobs[i].cmd[sizeof(g_jobs[i].cmd) - 1] = '\0';
            return i + 1; /* job id (1-based) */
        }
    }
    return -1;
}

static struct job *job_find(int job_id) {
    if (job_id < 1 || job_id > MAX_JOBS) {
        return NULL;
    }
    struct job *j = &g_jobs[job_id - 1];
    if (j->state == JOB_FREE) {
        return NULL;
    }
    return j;
}

/* 收割已完成的后台 jobs (非阻塞) */
static void jobs_reap(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_RUNNING) {
            int status;
            int ret = sys_waitpid(g_jobs[i].pid, &status, WNOHANG);
            if (ret > 0) {
                printf("[%d] Done    %s\n", i + 1, g_jobs[i].cmd);
                g_jobs[i].state = JOB_DONE;
            }
        }
    }
}

/* 重定向信息 */
struct redirect_info {
    const char *stdout_file;   /* > file */
    const char *stdin_file;    /* < file */
    int         stdout_append; /* >> */
};

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

    struct ipc_message reply = {0};
    sys_ipc_call(g_tty_ep, &msg, &reply, 100);
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
static void cmd_jobs(int argc, char **argv);
static void cmd_fg(int argc, char **argv);
static void cmd_bg(int argc, char **argv);

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
                                              {"jobs", cmd_jobs, "List background jobs"},
                                              {"fg", cmd_fg, "Resume job in foreground"},
                                              {"bg", cmd_bg, "Resume job in background"},
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
 * 解析命令行为 argc/argv,同时提取重定向
 */
static int parse_cmdline(char *line, char **argv, int max_args, struct redirect_info *redir) {
    int argc = 0;

    memset(redir, 0, sizeof(*redir));

    while (*line && argc < max_args - 1) {
        /* 跳过空格 */
        while (*line == ' ' || *line == '\t') {
            line++;
        }

        if (!*line) {
            break;
        }

        /* 检查输出重定向 >> */
        if (line[0] == '>' && line[1] == '>') {
            line += 2;
            while (*line == ' ' || *line == '\t') {
                line++;
            }
            redir->stdout_file   = line;
            redir->stdout_append = 1;
            while (*line && *line != ' ' && *line != '\t') {
                line++;
            }
            if (*line) {
                *line++ = '\0';
            }
            continue;
        }

        /* 检查输出重定向 > */
        if (*line == '>') {
            line++;
            while (*line == ' ' || *line == '\t') {
                line++;
            }
            redir->stdout_file   = line;
            redir->stdout_append = 0;
            while (*line && *line != ' ' && *line != '\t') {
                line++;
            }
            if (*line) {
                *line++ = '\0';
            }
            continue;
        }

        /* 检查输入重定向 < */
        if (*line == '<') {
            line++;
            while (*line == ' ' || *line == '\t') {
                line++;
            }
            redir->stdin_file = line;
            while (*line && *line != ' ' && *line != '\t') {
                line++;
            }
            if (*line) {
                *line++ = '\0';
            }
            continue;
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
 * 执行外部命令(支持重定向)
 * @param background 是否后台运行
 * @param redir      重定向信息
 */
static void run_external(const char *path, int argc, char **argv, int background,
                         struct redirect_info *redir) {
    struct proc_builder b;
    proc_init(&b, path);
    proc_inherit_named(&b);

    /* 根据重定向情况决定 stdio handle */
    if (redir->stdout_file) {
        int flags = O_WRONLY | O_CREAT;
        flags |= redir->stdout_append ? O_APPEND : O_TRUNC;
        int out_fd = open(redir->stdout_file, flags);
        if (out_fd < 0) {
            printf("%s: %s\n", redir->stdout_file, strerror(-out_fd));
            return;
        }
        /* 子进程 stdout 指向文件的 VFS endpoint
         * 但 VFS fd 没有内核 handle(handle=INVALID),
         * 无法直接传递. 改为传递 tty handle 然后在子进程中重定向.
         * 实际上, 对于 VFS 文件重定向, 我们需要不同的策略:
         * 不传递 stdout handle,让子进程继承后由 libc 初始化.
         * 这里先关闭 out_fd, 使用常规 stdio. */
        /* TODO: 完善 VFS 文件重定向(需要子进程侧支持) */
        close(out_fd);
        /* 暂时: 走常规继承 */
        proc_add_handle(&b, fd_get_handle(STDOUT_FILENO), HANDLE_STDIO_STDOUT);
    } else {
        proc_add_handle(&b, fd_get_handle(STDOUT_FILENO), HANDLE_STDIO_STDOUT);
    }

    proc_add_handle(&b, fd_get_handle(STDERR_FILENO), HANDLE_STDIO_STDERR);

    if (redir->stdin_file) {
        /* TODO: stdin 文件重定向 */
        proc_add_handle(&b, fd_get_handle(STDIN_FILENO), HANDLE_STDIO_STDIN);
    } else {
        proc_add_handle(&b, fd_get_handle(STDIN_FILENO), HANDLE_STDIO_STDIN);
    }

    for (int i = 0; i < argc; i++) {
        proc_add_arg(&b, argv[i]);
    }

    int pid = proc_spawn(&b);
    if (pid > 0) {
        vfs_copy_cwd_to_child(pid);
    }
    if (pid < 0) {
        printf("%s: %s\n", argv[0], strerror(-pid));
        return;
    }

    int job_id = job_add(pid, argv[0]);

    if (background) {
        if (job_id > 0) {
            printf("[%d] %d\n", job_id, pid);
        }
        return;
    }

    shell_set_foreground(pid);

    int status;
    sys_waitpid(pid, &status, 0);

    shell_set_foreground(0);

    /* 进程被 Ctrl+Z 暂停 → 标记为 STOPPED */
    if (status == -SIGTSTP || status == -SIGSTOP) {
        if (job_id > 0) {
            struct job *j = job_find(job_id);
            if (j) {
                j->state = JOB_STOPPED;
            }
            printf("\n[%d] Stopped    %s\n", job_id, argv[0]);
        }
        return;
    }

    /* 正常退出或被信号终止 */
    if (job_id > 0) {
        struct job *j = job_find(job_id);
        if (j) {
            j->state = JOB_DONE;
        }
    }
    if (status != 0) {
        printf("Process %d exited with status %d\n", pid, status);
    }
}

/**
 * 执行管道: cmd1 | cmd2
 */
static void run_pipeline(char *left_line, char *right_line) {
    int pfd[2];
    if (pipe(pfd) < 0) {
        printf("pipe: failed to create pipe\n");
        return;
    }

    /* 解析左右命令 */
    char                *left_argv[MAX_ARGS];
    char                *right_argv[MAX_ARGS];
    struct redirect_info left_redir, right_redir;

    int left_argc  = parse_cmdline(left_line, left_argv, MAX_ARGS, &left_redir);
    int right_argc = parse_cmdline(right_line, right_argv, MAX_ARGS, &right_redir);

    if (left_argc == 0 || right_argc == 0) {
        close(pfd[0]);
        close(pfd[1]);
        printf("Invalid pipe syntax\n");
        return;
    }

    /* 解析左命令的路径 */
    char left_path[256], right_path[256];
    if (!path_find(left_argv[0], left_path, sizeof(left_path))) {
        close(pfd[0]);
        close(pfd[1]);
        printf("Command not found: %s\n", left_argv[0]);
        return;
    }
    if (!path_find(right_argv[0], right_path, sizeof(right_path))) {
        close(pfd[0]);
        close(pfd[1]);
        printf("Command not found: %s\n", right_argv[0]);
        return;
    }

    /* 启动左命令: stdout → pipe 写端 */
    struct proc_builder lb;
    proc_init(&lb, left_path);
    proc_inherit_named(&lb);
    proc_add_handle(&lb, fd_get_handle(STDIN_FILENO), HANDLE_STDIO_STDIN);
    proc_add_handle(&lb, fd_get_handle(pfd[1]), HANDLE_STDIO_STDOUT);
    proc_add_handle(&lb, fd_get_handle(STDERR_FILENO), HANDLE_STDIO_STDERR);
    for (int i = 0; i < left_argc; i++) {
        proc_add_arg(&lb, left_argv[i]);
    }
    int left_pid = proc_spawn(&lb);
    if (left_pid > 0) {
        vfs_copy_cwd_to_child(left_pid);
    }

    /* 启动右命令: stdin → pipe 读端 */
    struct proc_builder rb;
    proc_init(&rb, right_path);
    proc_inherit_named(&rb);
    proc_add_handle(&rb, fd_get_handle(pfd[0]), HANDLE_STDIO_STDIN);
    proc_add_handle(&rb, fd_get_handle(STDOUT_FILENO), HANDLE_STDIO_STDOUT);
    proc_add_handle(&rb, fd_get_handle(STDERR_FILENO), HANDLE_STDIO_STDERR);
    for (int i = 0; i < right_argc; i++) {
        proc_add_arg(&rb, right_argv[i]);
    }
    int right_pid = proc_spawn(&rb);
    if (right_pid > 0) {
        vfs_copy_cwd_to_child(right_pid);
    }

    /* 父进程关闭 pipe 两端 */
    close(pfd[0]);
    close(pfd[1]);

    if (left_pid < 0) {
        printf("%s: %s\n", left_argv[0], strerror(-left_pid));
    }
    if (right_pid < 0) {
        printf("%s: %s\n", right_argv[0], strerror(-right_pid));
    }

    /* 等待两个子进程 */
    if (left_pid > 0) {
        shell_set_foreground(left_pid);
        int status;
        sys_waitpid(left_pid, &status, 0);
    }
    if (right_pid > 0) {
        shell_set_foreground(right_pid);
        int status;
        sys_waitpid(right_pid, &status, 0);
    }

    shell_set_foreground(0);
}

/**
 * 检查命令行中是否有管道符号 '|'
 * @return 管道位置指针, NULL=无管道
 */
static char *find_pipe(char *line) {
    for (char *p = line; *p; p++) {
        if (*p == '|') {
            return p;
        }
    }
    return NULL;
}

static void execute_command(char *line) {
    /* 先检查管道 */
    char *pipe_pos = find_pipe(line);
    if (pipe_pos) {
        *pipe_pos = '\0';
        run_pipeline(line, pipe_pos + 1);
        return;
    }

    struct redirect_info redir;
    char                *argv[MAX_ARGS];
    int                  argc = parse_cmdline(line, argv, MAX_ARGS, &redir);

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
        run_external(path, argc, argv, background, &redir);
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
    printf("\nRedirection: cmd > file, cmd < file, cmd1 | cmd2\n");
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

    printf("Error: 'run' command is deprecated (sys_spawn removed)\n");
    printf("Use regular commands to execute programs from /sys or /mnt\n");
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
        printf("Failed to kill pid %d: %s\n", pid, strerror(-ret));
    } else {
        printf("Sent SIGTERM to pid %d\n", pid);
    }
}

static void cmd_path(int argc, char **argv) {
    if (argc < 2) {
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
        printf("cd: %s: %s\n", path, strerror(-ret));
    }
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;

    char cwd[256];
    int  ret = vfs_getcwd(cwd, sizeof(cwd));
    if (ret < 0) {
        printf("pwd: %s\n", strerror(-ret));
    } else {
        printf("%s\n", cwd);
    }
}

static void cmd_jobs(int argc, char **argv) {
    (void)argc;
    (void)argv;

    jobs_reap();

    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (g_jobs[i].state == JOB_FREE || g_jobs[i].state == JOB_DONE) {
            continue;
        }
        const char *state_str = "Unknown";
        if (g_jobs[i].state == JOB_RUNNING) {
            state_str = "Running";
        } else if (g_jobs[i].state == JOB_STOPPED) {
            state_str = "Stopped";
        }
        printf("[%d] %-10s %s (pid %d)\n", i + 1, state_str, g_jobs[i].cmd, g_jobs[i].pid);
        found++;
    }
    if (!found) {
        printf("No jobs.\n");
    }
}

static void cmd_fg(int argc, char **argv) {
    int job_id = 0;

    if (argc >= 2) {
        const char *arg = argv[1];
        if (arg[0] == '%') {
            arg++;
        }
        job_id = simple_atoi(arg);
    } else {
        /* 无参数: 找最近一个 stopped/running job */
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (g_jobs[i].state == JOB_STOPPED || g_jobs[i].state == JOB_RUNNING) {
                job_id = i + 1;
                break;
            }
        }
    }

    struct job *j = job_find(job_id);
    if (!j) {
        printf("fg: no such job\n");
        return;
    }

    printf("%s\n", j->cmd);

    /* 发送 SIGCONT 恢复 */
    if (j->state == JOB_STOPPED) {
        sys_kill(j->pid, SIGCONT);
        j->state = JOB_RUNNING;
    }

    /* 设为前台并等待 */
    shell_set_foreground(j->pid);

    int status;
    sys_waitpid(j->pid, &status, 0);

    shell_set_foreground(0);

    if (status == -SIGTSTP || status == -SIGSTOP) {
        j->state = JOB_STOPPED;
        printf("\n[%d] Stopped    %s\n", job_id, j->cmd);
    } else {
        j->state = JOB_DONE;
        if (status != 0) {
            printf("Process %d exited with status %d\n", j->pid, status);
        }
    }
}

static void cmd_bg(int argc, char **argv) {
    int job_id = 0;

    if (argc >= 2) {
        const char *arg = argv[1];
        if (arg[0] == '%') {
            arg++;
        }
        job_id = simple_atoi(arg);
    } else {
        for (int i = MAX_JOBS - 1; i >= 0; i--) {
            if (g_jobs[i].state == JOB_STOPPED) {
                job_id = i + 1;
                break;
            }
        }
    }

    struct job *j = job_find(job_id);
    if (!j || j->state != JOB_STOPPED) {
        printf("bg: no stopped job\n");
        return;
    }

    sys_kill(j->pid, SIGCONT);
    j->state = JOB_RUNNING;
    printf("[%d] %s &\n", job_id, j->cmd);
}

int main(int argc, char **argv) {
    char line[MAX_LINE];

    /* 解析命令行参数 */
    const char *tty_name = "tty0";
    const char *svc_name = "shell";
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "--tty=", 6) == 0) {
            tty_name = argv[i] + 6;
        } else if (strncmp(argv[i], "--svc=", 6) == 0) {
            svc_name = argv[i] + 6;
        }
    }

    /* 查找 handles */
    g_tty_ep = env_get_handle(tty_name);
    g_vfs_ep = env_get_handle("vfs_ep");

    /* 重新绑定 stdio 到指定的 tty:
     * 通过 dup2 把 tty handle 设置到 fd 0/1/2,
     * 然后 stdio FILE 自然走 fd 层到正确的 tty. */
    if (g_tty_ep != HANDLE_INVALID) {
        /* 创建新的 fd 指向 tty_ep, 然后 dup2 到 0/1/2 */
        int tty_fd = fd_alloc();
        if (tty_fd >= 0) {
            fd_install(tty_fd, g_tty_ep, 0, 0, FD_FLAG_READ | FD_FLAG_WRITE);
            if (tty_fd != STDIN_FILENO) {
                dup2(tty_fd, STDIN_FILENO);
            }
            if (tty_fd != STDOUT_FILENO) {
                dup2(tty_fd, STDOUT_FILENO);
            }
            if (tty_fd != STDERR_FILENO) {
                dup2(tty_fd, STDERR_FILENO);
            }
            if (tty_fd > STDERR_FILENO) {
                close(tty_fd);
            }
            /* dup2 创建了新 handle, 更新 g_tty_ep 指向有效的 */
            g_tty_ep = fd_get_handle(STDIN_FILENO);
        }
    }

    /* 初始化 VFS 客户端 */
    vfs_client_init(g_vfs_ep);

    /* 初始化 PATH */
    path_init();

    svc_notify_ready(svc_name);

    /* 输出欢迎信息 */
    printf("\nXnix Shell\n");
    printf("Type 'help' for available commands.\n\n");

    while (1) {
        jobs_reap(); /* 收割已完成的后台 jobs */

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
