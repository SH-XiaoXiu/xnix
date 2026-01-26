#include <arch/hal/feature.h>

#include <asm/multiboot.h>
#include <xnix/boot.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

static uint32_t boot_parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

static bool boot_cmdline_has_kv(const char *cmdline, const char *key, const char *value) {
    size_t key_len = strlen(key);
    size_t val_len = strlen(value);

    if (!cmdline || !key || !value) {
        return false;
    }

    const char *p = cmdline;
    while (*p) {
        while (*p == ' ') {
            p++;
        }

        if (!strncmp(p, key, key_len) && p[key_len] == '=' &&
            !strncmp(p + key_len + 1, value, val_len) &&
            ((p + key_len + 1 + val_len)[0] == '\0' || (p + key_len + 1 + val_len)[0] == ' ')) {
            return true;
        }

        while (*p && *p != ' ') {
            p++;
        }
    }

    return false;
}

static bool boot_cmdline_get_u32(const char *cmdline, const char *key, uint32_t *out) {
    size_t key_len;

    if (!cmdline || !key || !out) {
        return false;
    }

    key_len = strlen(key);

    const char *p = cmdline;
    while (*p) {
        while (*p == ' ') {
            p++;
        }

        if (!strncmp(p, key, key_len) && p[key_len] == '=') {
            const char *v = p + key_len + 1;
            if (*v < '0' || *v > '9') {
                return false;
            }
            *out = boot_parse_u32(v);
            return true;
        }

        while (*p && *p != ' ') {
            p++;
        }
    }

    return false;
}

static uint32_t g_boot_initmod_index = 0;

uint32_t boot_get_initmod_index(void) {
    return g_boot_initmod_index;
}

static uint32_t boot_compute_ram_mb(uint32_t magic, const struct multiboot_info *mb_info) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !mb_info) {
        return 0;
    }

    if ((mb_info->flags & MULTIBOOT_INFO_MEM_MAP) && mb_info->mmap_length && mb_info->mmap_addr) {
        uint64_t total_kb = 0;
        uint32_t off      = 0;
        while (off < mb_info->mmap_length) {
            struct multiboot_mmap_entry *e =
                (struct multiboot_mmap_entry *)(uintptr_t)(mb_info->mmap_addr + off);
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE && e->len) {
                total_kb += e->len / 1024ULL;
            }
            off += e->size + sizeof(e->size);
        }
        return (uint32_t)(total_kb / 1024ULL);
    }

    if (mb_info->flags & MULTIBOOT_INFO_MEMORY) {
        uint32_t total_kb = mb_info->mem_upper + 1024;
        return total_kb / 1024;
    }

    return 0;
}

__attribute__((weak)) void boot_init(uint32_t magic, const struct multiboot_info *mb_info) {
    struct hal_features features;
    const char         *cmdline = NULL;

    hal_probe_features(&features);

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC && mb_info) {
        features.ram_size_mb = boot_compute_ram_mb(magic, mb_info);

        if (mb_info->flags & MULTIBOOT_INFO_CMDLINE) {
            cmdline = (const char *)mb_info->cmdline;
        }
    }

    if (boot_cmdline_has_kv(cmdline, "xnix.mmu", "off")) {
        features.flags &= ~HAL_FEATURE_MMU;
        pr_info("Boot: forced MMU off via cmdline");
    }

    if (boot_cmdline_has_kv(cmdline, "xnix.smp", "off")) {
        features.flags &= ~HAL_FEATURE_SMP;
        features.cpu_count = 1;
        pr_info("Boot: forced SMP off via cmdline");
    }

    if (cmdline) {
        uint32_t init_mod_index;
        if (boot_cmdline_get_u32(cmdline, "xnix.initmod", &init_mod_index)) {
            g_boot_initmod_index = init_mod_index;
            pr_info("Boot: xnix.initmod=%u", g_boot_initmod_index);
        }
    }

    extern struct hal_features g_hal_features;
    memcpy(&g_hal_features, &features, sizeof(features));
}
