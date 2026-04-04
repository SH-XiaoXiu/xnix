/**
 * @file cap.c
 * @brief 能力 (Capability) 检查实现
 *
 * 整个能力系统的内核实现. 基于 uint32_t 位图做能力检查与子集验证.
 */

#include <xnix/cap.h>
#include <xnix/mm.h>
#include <xnix/process_def.h>
#include <xnix/string.h>

bool cap_check(struct process *proc, uint32_t cap) {
    if (!proc) {
        return false;
    }
    return (proc->cap_mask & cap) == cap;
}

bool cap_check_ioport(struct process *proc, uint16_t port) {
    if (!proc) {
        return false;
    }
    if (!(proc->cap_mask & CAP_IO_PORT)) {
        return false;
    }
    if (!proc->ioport_bitmap) {
        return false;
    }
    return (proc->ioport_bitmap[port / 8] >> (port % 8)) & 1;
}

bool cap_check_irq(struct process *proc, uint8_t irq) {
    if (!proc || irq >= 32) {
        return false;
    }
    if (!(proc->cap_mask & CAP_IRQ)) {
        return false;
    }
    return (proc->irq_mask >> irq) & 1;
}

bool cap_is_subset(uint32_t child_caps, uint32_t parent_caps) {
    return (child_caps & parent_caps) == child_caps;
}

uint8_t *cap_build_ioport_bitmap(const struct spawn_caps *caps) {
    if (!caps || caps->ioport_count == 0) {
        return NULL;
    }

    /* 65536 ports / 8 = 8192 bytes */
    uint8_t *bitmap = kzalloc(8192);
    if (!bitmap) {
        return NULL;
    }

    for (uint8_t i = 0; i < caps->ioport_count && i < SPAWN_IOPORT_RANGES_MAX; i++) {
        uint32_t start = caps->ioports[i].start;
        uint32_t end   = caps->ioports[i].end;
        for (uint32_t port = start; port <= end && port < 65536; port++) {
            bitmap[port / 8] |= (1u << (port % 8));
        }
    }

    return bitmap;
}

uint32_t cap_build_irq_mask(const struct spawn_caps *caps) {
    if (!caps) {
        return 0;
    }

    uint32_t mask = 0;
    for (uint8_t i = 0; i < caps->irq_count && i < SPAWN_IRQS_MAX; i++) {
        if (caps->irqs[i] < 32) {
            mask |= (1u << caps->irqs[i]);
        }
    }
    return mask;
}
