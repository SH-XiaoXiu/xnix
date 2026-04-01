/**
 * @file platform.c
 * @brief PC 平台描述符
 */

#include <plat/platform.h>
#include <plat/early_dev.h>

/* 外部驱动注册函数 */
extern void pic_register(void);
extern void pit_register(void);
extern void apic_register(void);          /* ioapic.c */
extern void lapic_timer_register(void);
extern void serial_console_register(void);
extern void vga_console_register(void);
extern void ps2_register(void);
extern void fb_console_register(void);

/* 外部探测函数 */
extern bool fb_available(void);

static int pic_probe(void) {
    /* PIC 总是可用 */
    return 1;
}

static int pit_probe(void) {
    /* PIT 总是可用 */
    return 1;
}

static int apic_probe(void) {
    /* APIC 由 HAL 特性检测决定 */
    extern struct hal_features g_hal_features;
    return (g_hal_features.flags & HAL_FEATURE_APIC) != 0;
}

static int fb_probe(void) {
    /* 检测 framebuffer 是否可用 */
    return fb_available() ? 1 : 0;
}

static const struct early_device pc_early_devices[] = {
    /* 控制台驱动 - 优先注册,不需要 probe */
    { "fb_console",     0, NULL,             fb_console_register    },
    { "vga_console",   10, NULL,             vga_console_register   },
    { "serial_console",10, NULL,             serial_console_register},

    /* 中断控制器和定时器 */
    { "pic",           20, pic_probe,        pic_register           },
    { "pit",           20, pit_probe,        pit_register           },
    { "apic",          25, apic_probe,       apic_register          },
    { "lapic_timer",   25, apic_probe,       lapic_timer_register   },

    /* 输入设备 */
    { "ps2",           30, NULL,             ps2_register           },

    { NULL, 0, NULL, NULL }  /* 结束标记 */
};

static int pc_early_init(void) {
    /* 平台特定的早期初始化 */
    return 0;
}

static int pc_driver_init(void) {
    /* 初始化早期设备 */
    return early_devices_init(pc_early_devices);
}

const struct platform_desc platform_pc = {
    .name = "pc",
    .devices = pc_early_devices,
    .early_init = pc_early_init,
    .driver_init = pc_driver_init,
};

const struct platform_desc *platform_get(void) {
    return &platform_pc;
}

/* 早期设备初始化实现 */
int early_devices_init(const struct early_device *devices) {
    if (!devices) {
        return -1;
    }

    /* 按优先级遍历设备 */
    for (const struct early_device *dev = devices; dev->name; dev++) {
        /* 探测设备是否可用 */
        if (dev->probe && !dev->probe()) {
            continue;
        }

        /* 初始化设备 */
        if (dev->init) {
            dev->init();
        }
    }

    return 0;
}
