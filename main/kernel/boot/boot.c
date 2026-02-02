#include <arch/hal/feature.h>

#include <asm/mmu.h>
#include <asm/multiboot.h>
#include <xnix/boot.h>
#include <xnix/driver.h>
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

static uint32_t g_boot_initmod_index   = 0;
static uint32_t g_boot_serialmod_index = 0xFFFFFFFFu;

/* 保存 multiboot 模块信息 */
static struct multiboot_mod_list *g_boot_modules      = NULL;
static uint32_t                   g_boot_module_count = 0;

/* 保存 framebuffer 信息 */
static struct boot_framebuffer_info g_boot_fb;
static bool                         g_boot_fb_valid = false;

uint32_t boot_get_initmod_index(void) {
    return g_boot_initmod_index;
}

uint32_t boot_get_serialmod_index(void) {
    return g_boot_serialmod_index;
}

uint32_t boot_get_module_count(void) {
    return g_boot_module_count;
}

int boot_get_module(uint32_t index, void **out_addr, uint32_t *out_size) {
    if (index >= g_boot_module_count || !g_boot_modules) {
        return -1;
    }
    if (out_addr) {
        *out_addr = (void *)(uintptr_t)g_boot_modules[index].mod_start;
    }
    if (out_size) {
        *out_size = g_boot_modules[index].mod_end - g_boot_modules[index].mod_start;
    }
    return 0;
}

int boot_get_framebuffer(struct boot_framebuffer_info *info) {
    if (!g_boot_fb_valid || !info) {
        return -1;
    }
    *info = g_boot_fb;
    return 0;
}

static uint32_t boot_compute_ram_mb(uint32_t magic, const struct multiboot_info *mb_info) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !mb_info) {
        return 0;
    }

    if ((mb_info->flags & MULTIBOOT_INFO_MEM_MAP) && mb_info->mmap_length && mb_info->mmap_addr) {
        uint64_t total_kb = 0;
        uint32_t off      = 0;
        while (off < mb_info->mmap_length) {
            /* mmap_addr 是物理地址,需要转换为虚拟地址 */
            struct multiboot_mmap_entry *e =
                (struct multiboot_mmap_entry *)PHYS_TO_VIRT(mb_info->mmap_addr + off);
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
            /* cmdline 是物理地址,需要转换为虚拟地址 */
            cmdline = (const char *)PHYS_TO_VIRT(mb_info->cmdline);
            /* 保存命令行供驱动选择使用 */
            boot_save_cmdline(cmdline);
        }

        /* 保存模块信息 */
        if ((mb_info->flags & MULTIBOOT_INFO_MODS) && mb_info->mods_count > 0) {
            /* mods_addr 是物理地址,需要转换为虚拟地址 */
            g_boot_modules      = (struct multiboot_mod_list *)PHYS_TO_VIRT(mb_info->mods_addr);
            g_boot_module_count = mb_info->mods_count;
        }

        /* 保存 framebuffer 信息 */
        if (mb_info->flags & MULTIBOOT_INFO_FRAMEBUFFER) {
            g_boot_fb.addr   = mb_info->framebuffer_addr;
            g_boot_fb.pitch  = mb_info->framebuffer_pitch;
            g_boot_fb.width  = mb_info->framebuffer_width;
            g_boot_fb.height = mb_info->framebuffer_height;
            g_boot_fb.bpp    = mb_info->framebuffer_bpp;
            g_boot_fb.type   = mb_info->framebuffer_type;

            if (mb_info->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
                g_boot_fb.red_pos    = mb_info->color_info.rgb.red_field_position;
                g_boot_fb.red_size   = mb_info->color_info.rgb.red_mask_size;
                g_boot_fb.green_pos  = mb_info->color_info.rgb.green_field_position;
                g_boot_fb.green_size = mb_info->color_info.rgb.green_mask_size;
                g_boot_fb.blue_pos   = mb_info->color_info.rgb.blue_field_position;
                g_boot_fb.blue_size  = mb_info->color_info.rgb.blue_mask_size;
            }

            /* 如果颜色信息无效(任一 size 为 0),使用标准 BGRA 默认值 */
            if (g_boot_fb.bpp >= 24 && (g_boot_fb.red_size == 0 || g_boot_fb.green_size == 0 ||
                                        g_boot_fb.blue_size == 0)) {
                g_boot_fb.blue_pos   = 0;
                g_boot_fb.blue_size  = 8;
                g_boot_fb.green_pos  = 8;
                g_boot_fb.green_size = 8;
                g_boot_fb.red_pos    = 16;
                g_boot_fb.red_size   = 8;
            }

            g_boot_fb_valid = true;
            pr_info("Boot: framebuffer %ux%u@%u at 0x%x", g_boot_fb.width, g_boot_fb.height,
                    g_boot_fb.bpp, (uint32_t)g_boot_fb.addr);
        }
    }

    if (boot_cmdline_has_kv(cmdline, "xnix.mmu", "off")) {
        features.flags &= ~HAL_FEATURE_MMU;
        pr_info("Boot: forced MMU off via cmdline");
    }

    if (boot_cmdline_has_kv(cmdline, "xnix.smp", "off")) {
        features.flags &= ~HAL_FEATURE_SMP;
        features.cpu_count = 1;
        hal_force_disable_smp();
        pr_info("Boot: forced SMP off via cmdline");
    }

    if (cmdline) {
        uint32_t init_mod_index;
        if (boot_cmdline_get_u32(cmdline, "xnix.initmod", &init_mod_index)) {
            g_boot_initmod_index = init_mod_index;
            pr_info("Boot: xnix.initmod=%u", g_boot_initmod_index);
        }

        uint32_t serial_mod_index;
        if (boot_cmdline_get_u32(cmdline, "xnix.serialmod", &serial_mod_index)) {
            g_boot_serialmod_index = serial_mod_index;
            pr_info("Boot: xnix.serialmod=%u", g_boot_serialmod_index);
        }
    }

    extern struct hal_features g_hal_features;
    memcpy(&g_hal_features, &features, sizeof(features));
}
