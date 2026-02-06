#ifndef XNIX_BOOT_INTERNAL_H
#define XNIX_BOOT_INTERNAL_H

#include <arch/hal/feature.h>

#include <asm/multiboot.h>
#include <xnix/types.h>

void boot_multiboot_collect(uint32_t magic, const struct multiboot_info *mb_info,
                            struct hal_features *features);
void boot_policy_apply(struct hal_features *features);

bool boot_kv_get_value(const char *cmdline, const char *key, char *out, size_t out_sz);

#endif
