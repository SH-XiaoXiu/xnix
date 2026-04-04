/**
 * @file main.c
 * @brief lscaps - 列出当前进程的能力位图
 */

#include <stdio.h>
#include <xnix/abi/cap.h>
#include <xnix/syscall.h>

struct cap_name {
    uint32_t    bit;
    const char *name;
};

static const struct cap_name caps[] = {
    { CAP_IPC_SEND,      "ipc_send"      },
    { CAP_IPC_RECV,      "ipc_recv"      },
    { CAP_IPC_ENDPOINT,  "ipc_endpoint"  },
    { CAP_PROCESS_EXEC,  "process_exec"  },
    { CAP_HANDLE_GRANT,  "handle_grant"  },
    { CAP_MM_MMAP,       "mm_mmap"       },
    { CAP_IO_PORT,       "io_port"       },
    { CAP_IRQ,           "irq"           },
    { CAP_DEBUG_CONSOLE, "debug_console" },
    { CAP_KERNEL_KMSG,   "kernel_kmsg"   },
    { CAP_CAP_DELEGATE,  "cap_delegate"  },
};

#define CAP_TABLE_SIZE (sizeof(caps) / sizeof(caps[0]))

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t mask = sys_cap_query();

    printf("CAPABILITY       STATUS\n");
    for (unsigned i = 0; i < CAP_TABLE_SIZE; i++) {
        printf("%-16s %s\n", caps[i].name,
               (mask & caps[i].bit) ? "yes" : "no");
    }
    printf("\nraw: 0x%08x\n", mask);
    return 0;
}
