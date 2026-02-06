#include "boot_internal.h"

#include <arch/hal/feature.h>

#include <xnix/boot.h>
#include <xnix/string.h>

__attribute__((weak)) void boot_init(uint32_t magic, const struct multiboot_info *mb_info) {
    struct hal_features features;

    hal_probe_features(&features);

    boot_multiboot_collect(magic, mb_info, &features);
    boot_policy_apply(&features);

    extern struct hal_features g_hal_features;
    memcpy(&g_hal_features, &features, sizeof(features));
}
