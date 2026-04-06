/**
 * @file main.c
 * @brief Inject text into a tty as input
 */

#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/protocol/tty.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

static void print_usage(void) {
    printf("Usage: ttysend /dev/ttyN <text>\n");
}

static size_t decode_escapes(const char *src, char *dst, size_t max) {
    size_t out = 0;

    while (*src && out < max) {
        if (src[0] == '\\' && src[1] != '\0') {
            src++;
            switch (*src) {
            case 'n':
                dst[out++] = '\n';
                src++;
                continue;
            case 'r':
                dst[out++] = '\r';
                src++;
                continue;
            case 't':
                dst[out++] = '\t';
                src++;
                continue;
            case '\\':
                dst[out++] = '\\';
                src++;
                continue;
            default:
                dst[out++] = '\\';
                if (out < max) {
                    dst[out++] = *src++;
                }
                continue;
            }
        }

        dst[out++] = *src++;
    }

    return out;
}

static const char *strip_wrapping_quotes(const char *src, size_t *len) {
    if (*len >= 2) {
        char first = src[0];
        char last = src[*len - 1];
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            src++;
            *len -= 2;
        }
    }
    return src;
}

int main(int argc, char **argv) {
    int fd;
    struct fd_entry *ent;
    struct ipc_message msg = {0};
    char decoded[256];
    size_t decoded_len;
    int ret;

    if (argc < 3) {
        print_usage();
        return 1;
    }

    fd = vfs_open(argv[1], 0);
    if (fd < 0) {
        printf("ttysend: cannot open '%s': %s\n", argv[1], strerror(-fd));
        return 1;
    }

    ent = fd_get(fd);
    if (!ent) {
        printf("ttysend: invalid fd\n");
        vfs_close(fd);
        return 1;
    }

    {
        const char *text = argv[2];
        size_t text_len = strlen(text);
        char raw[256];

        text = strip_wrapping_quotes(text, &text_len);
        if (text_len >= sizeof(raw)) {
            text_len = sizeof(raw) - 1;
        }
        memcpy(raw, text, text_len);
        raw[text_len] = '\0';

        decoded_len = decode_escapes(raw, decoded, sizeof(decoded));
    }

    msg.regs.data[0] = TTY_OP_INPUT;
    msg.buffer.data = (uint64_t)(uintptr_t)decoded;
    msg.buffer.size = (uint32_t)decoded_len;

    ret = sys_ipc_send(ent->handle, &msg, 1000);
    if (ret < 0) {
        printf("ttysend: inject failed: %d\n", ret);
        vfs_close(fd);
        return 1;
    }

    vfs_close(fd);
    return 0;
}
