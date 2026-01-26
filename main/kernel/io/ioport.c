#include <kernel/capability/capability.h>
#include <kernel/io/ioport.h>
#include <xnix/mm.h>

#include "arch/cpu.h"

void ioport_ref(void *ptr) {
    struct ioport_range *r = ptr;
    if (!r) {
        return;
    }
    uint32_t flags = cpu_irq_save();
    r->refcount++;
    cpu_irq_restore(flags);
}

void ioport_unref(void *ptr) {
    struct ioport_range *r = ptr;
    if (!r) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    r->refcount--;
    if (r->refcount == 0) {
        cpu_irq_restore(flags);
        kfree(r);
        return;
    }
    cpu_irq_restore(flags);
}

void ioport_init(void) {
    cap_register_type(CAP_TYPE_IOPORT, ioport_ref, ioport_unref);
}

cap_handle_t ioport_create_range(struct process *owner, uint16_t start, uint16_t end,
                                 cap_rights_t rights) {
    if (!owner) {
        return CAP_HANDLE_INVALID;
    }
    if (start > end) {
        return CAP_HANDLE_INVALID;
    }
    if ((rights & (CAP_READ | CAP_WRITE)) == 0) {
        return CAP_HANDLE_INVALID;
    }

    struct ioport_range *r = kzalloc(sizeof(*r));
    if (!r) {
        return CAP_HANDLE_INVALID;
    }

    r->start    = start;
    r->end      = end;
    r->refcount = 0;

    cap_handle_t h = cap_alloc(owner, CAP_TYPE_IOPORT, r, rights);
    if (h == CAP_HANDLE_INVALID) {
        kfree(r);
        return CAP_HANDLE_INVALID;
    }

    return h;
}

bool ioport_range_contains(const struct ioport_range *r, uint16_t port) {
    if (!r) {
        return false;
    }
    return port >= r->start && port <= r->end;
}
