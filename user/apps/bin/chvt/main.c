#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xnix/protocol/tty.h>

static void usage(void) {
    printf("Usage: chvt <tty-id>\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage();
        return 1;
    }

    char *end = NULL;
    long tty_id = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || tty_id < 0) {
        usage();
        return 1;
    }

    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) {
        printf("chvt: cannot open /dev/tty0: %d\n", fd);
        return 1;
    }

    if (ioctl(fd, TTY_IOCTL_SET_ACTIVE_TTY, (unsigned int)tty_id) < 0) {
        printf("chvt: tty%d is not a display VT or switch failed\n", (int)tty_id);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
