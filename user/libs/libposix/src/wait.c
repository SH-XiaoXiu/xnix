#include <sys/wait.h>
#include <xnix/syscall.h>

int waitpid(int pid, int *status, int options) {
    return sys_waitpid(pid, status, options);
}

int wait(int *status) {
    return waitpid(-1, status, 0);
}
