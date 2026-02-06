#include "boot_internal.h"

#include <arch/hal/feature.h>

#include <xnix/boot.h>
#include <xnix/stdio.h>

void boot_policy_apply(struct hal_features *features) {
    if (!features) {
        return;
    }

    if (boot_cmdline_has_kv("xnix.mmu", "off")) {
        features->flags &= ~HAL_FEATURE_MMU;
        pr_info("Boot: forced MMU off via cmdline");
    }

    if (boot_cmdline_has_kv("xnix.smp", "off")) {
        features->flags &= ~HAL_FEATURE_SMP;
        features->cpu_count = 1;
        hal_force_disable_smp();
        pr_info("Boot: forced SMP off via cmdline");
    }
}
