#include "boot_internal.h"

#include <asm/mmu.h>
#include <asm/multiboot.h>
#include <xnix/boot.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

static struct multiboot_mod_list *g_boot_modules      = NULL;
static uint32_t                   g_boot_module_count = 0;

static struct boot_framebuffer_info g_boot_fb;
static bool                         g_boot_fb_valid = false;

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

const char *boot_get_module_cmdline(uint32_t index) {
    if (index >= g_boot_module_count || !g_boot_modules) {
        return NULL;
    }
    uint32_t cmdline_phys = g_boot_modules[index].cmdline;
    if (cmdline_phys == 0) {
        return NULL;
    }
    return (const char *)PHYS_TO_VIRT(cmdline_phys);
}

const char *boot_get_module_cmdline_by_name(const char *name) {
    if (!name || !name[0]) {
        return NULL;
    }

    for (uint32_t i = 0; i < g_boot_module_count; i++) {
        const char *cmdline = boot_get_module_cmdline(i);
        if (!cmdline) {
            continue;
        }

        char module_name[16];
        if (!boot_kv_get_value(cmdline, "name", module_name, sizeof(module_name))) {
            continue;
        }
        if (strcmp(module_name, name) != 0) {
            continue;
        }

        return cmdline;
    }

    return NULL;
}

int boot_find_module_by_name(const char *name, void **out_addr, uint32_t *out_size) {
    if (!name || !name[0]) {
        return -1;
    }

    for (uint32_t i = 0; i < g_boot_module_count; i++) {
        const char *cmdline = boot_get_module_cmdline(i);
        if (!cmdline) {
            continue;
        }

        char module_name[16];
        if (!boot_kv_get_value(cmdline, "name", module_name, sizeof(module_name))) {
            continue;
        }
        if (strcmp(module_name, name) != 0) {
            continue;
        }

        return boot_get_module(i, out_addr, out_size);
    }

    return -1;
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

void boot_multiboot_collect(uint32_t magic, const struct multiboot_info *mb_info,
                            struct hal_features *features) {
    boot_cmdline_set(NULL);

    g_boot_modules      = NULL;
    g_boot_module_count = 0;
    g_boot_fb_valid     = false;
    memset(&g_boot_fb, 0, sizeof(g_boot_fb));

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !mb_info) {
        return;
    }

    if (features) {
        features->ram_size_mb = boot_compute_ram_mb(magic, mb_info);
    }

    if (mb_info->flags & MULTIBOOT_INFO_CMDLINE) {
        const char *cmdline = (const char *)PHYS_TO_VIRT(mb_info->cmdline);
        boot_cmdline_set(cmdline);
    }

    if ((mb_info->flags & MULTIBOOT_INFO_MODS) && mb_info->mods_count > 0) {
        g_boot_modules      = (struct multiboot_mod_list *)PHYS_TO_VIRT(mb_info->mods_addr);
        g_boot_module_count = mb_info->mods_count;
    }

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

        if (g_boot_fb.bpp >= 24 &&
            (g_boot_fb.red_size == 0 || g_boot_fb.green_size == 0 || g_boot_fb.blue_size == 0)) {
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
