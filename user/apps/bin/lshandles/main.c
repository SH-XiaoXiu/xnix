/**
 * @file main.c
 * @brief lshandles - 列出当前进程的所有 handle
 */

#include <stdio.h>
#include <xnix/abi/handle.h>
#include <xnix/syscall.h>

static const char *type_str(handle_type_t type) {
    switch (type) {
    case HANDLE_ENDPOINT:     return "ENDPOINT";
    case HANDLE_PHYSMEM:      return "PHYSMEM";
    case HANDLE_EVENT: return "EVENT";
    case HANDLE_PIPE_READ: return "PIPE_R";
    case HANDLE_PIPE_WRITE: return "PIPE_W";
    case HANDLE_THREAD:       return "THREAD";
    case HANDLE_PROCESS:      return "PROCESS";
    default:                  return "?";
    }
}

static void rights_str(uint32_t rights, char *buf) {
    buf[0] = (rights & HANDLE_RIGHT_READ)      ? 'R' : '-';
    buf[1] = (rights & HANDLE_RIGHT_WRITE)     ? 'W' : '-';
    buf[2] = (rights & HANDLE_RIGHT_EXECUTE)   ? 'X' : '-';
    buf[3] = (rights & HANDLE_RIGHT_TRANSFER)  ? 'T' : '-';
    buf[4] = (rights & HANDLE_RIGHT_DUPLICATE) ? 'D' : '-';
    buf[5] = '\0';
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    struct abi_handle_info handles[64] = {0};
    int count = sys_handle_list(handles, 64);
    if (count < 0) {
        printf("sys_handle_list failed\n");
        return 1;
    }

    printf("HANDLE  TYPE       RIGHTS  NAME\n");
    for (int i = 0; i < count; i++) {
        char r[6];
        rights_str(handles[i].rights, r);
        printf("%-7u %-10s %s   %s\n",
               handles[i].handle,
               type_str(handles[i].type),
               r,
               handles[i].name[0] ? handles[i].name : "(unnamed)");
    }
    printf("total: %d handles\n", count);
    return 0;
}
