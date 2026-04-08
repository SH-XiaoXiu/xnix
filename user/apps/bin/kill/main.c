/**
 * @file main.c
 * @brief kill - 发送信号给进程
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>

static int simple_atoi(const char *s) {
    int n   = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: kill <pid> [signal]\n");
        return 1;
    }

    int pid = simple_atoi(argv[1]);
    int sig = SIGTERM;

    if (argc >= 3) {
        sig = simple_atoi(argv[2]);
    }

    if (pid <= 1) {
        printf("kill: cannot kill pid %d\n", pid);
        return 1;
    }

    int ret = sys_kill(pid, sig);
    if (ret < 0) {
        printf("kill: pid %d: %s\n", pid, strerror(-ret));
        return 1;
    }

    return 0;
}
