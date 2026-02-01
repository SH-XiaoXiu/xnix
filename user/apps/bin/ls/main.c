/**
 * @file main.c
 * @brief 列出目录内容
 */

#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

/* ANSI 颜色代码 */
#define COLOR_RESET  "\x1b[0m"
#define COLOR_DIR    "\x1b[33m"   /* 黄色 */
#define COLOR_EXEC   "\x1b[32m"   /* 绿色 */
#define COLOR_FILE   "\x1b[0m"    /* 默认色 */

#define MAX_ENTRIES 256

struct ls_entry {
    char     name[VFS_NAME_MAX + 1];
    uint32_t type;
    int      is_exec;
};

static struct ls_entry entries[MAX_ENTRIES];
static int             entry_count = 0;

/**
 * 转换为小写
 */
static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

/**
 * 将字符串转换为小写
 */
static void str_to_lower(char *s) {
    while (*s) {
        *s = to_lower(*s);
        s++;
    }
}

/**
 * 不区分大小写比较扩展名
 */
static int ends_with_elf(const char *name) {
    size_t len = strlen(name);
    if (len < 4) {
        return 0;
    }
    const char *ext = name + len - 4;
    return (to_lower(ext[0]) == '.' && to_lower(ext[1]) == 'e' && to_lower(ext[2]) == 'l' &&
            to_lower(ext[3]) == 'f');
}

/**
 * 检查文件是否可执行(通过扩展名)
 */
static int is_executable(const char *name) {
    return ends_with_elf(name);
}

/**
 * 字符串比较(区分大小写)
 */
static int strcmp_case(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return (unsigned char)*a - (unsigned char)*b;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * 比较函数: 目录在前,文件在后,同类型按字母顺序
 */
static int compare_entries(const struct ls_entry *a, const struct ls_entry *b) {
    /* 目录优先 */
    if (a->type == VFS_TYPE_DIR && b->type != VFS_TYPE_DIR) {
        return -1;
    }
    if (a->type != VFS_TYPE_DIR && b->type == VFS_TYPE_DIR) {
        return 1;
    }
    /* 同类型按名称排序 */
    return strcmp_case(a->name, b->name);
}

/**
 * 简单的冒泡排序
 */
static void sort_entries(void) {
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = 0; j < entry_count - 1 - i; j++) {
            if (compare_entries(&entries[j], &entries[j + 1]) > 0) {
                struct ls_entry tmp = entries[j];
                entries[j]          = entries[j + 1];
                entries[j + 1]      = tmp;
            }
        }
    }
}

/**
 * 获取条目的颜色
 */
static const char *get_color(const struct ls_entry *e) {
    if (e->type == VFS_TYPE_DIR) {
        return COLOR_DIR;
    }
    if (e->is_exec) {
        return COLOR_EXEC;
    }
    return COLOR_FILE;
}

int main(int argc, char **argv) {
    const char *path = ".";

    if (argc > 1) {
        path = argv[1];
    }

    int fd = sys_opendir(path);
    if (fd < 0) {
        printf("ls: cannot open '%s': error %d\n", path, fd);
        return 1;
    }

    /* 收集所有条目 */
    struct vfs_dirent dirent;
    uint32_t          index = 0;
    entry_count             = 0;

    while (sys_readdir(fd, index, &dirent) == 0 && entry_count < MAX_ENTRIES) {
        strcpy(entries[entry_count].name, dirent.name);
        str_to_lower(entries[entry_count].name); /* 转换为小写 */
        entries[entry_count].type    = dirent.type;
        entries[entry_count].is_exec = (dirent.type == VFS_TYPE_FILE && is_executable(dirent.name));
        entry_count++;
        index++;
    }

    sys_close(fd);

    if (entry_count == 0) {
        printf("(empty)\n");
        return 0;
    }

    /* 排序 */
    sort_entries();

    /* 输出 */
    for (int i = 0; i < entry_count; i++) {
        const char *color = get_color(&entries[i]);
        if (entries[i].type == VFS_TYPE_DIR) {
            printf("%s%s/%s\n", color, entries[i].name, COLOR_RESET);
        } else {
            printf("%s%s%s\n", color, entries[i].name, COLOR_RESET);
        }
    }

    return 0;
}
