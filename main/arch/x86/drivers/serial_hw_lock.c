#include <drivers/serial_hw_lock.h>

#include <xnix/sync.h>

static spinlock_t g_serial_hw_lock = SPINLOCK_INIT;

uint32_t serial_hw_lock_irqsave(void) {
    return spin_lock_irqsave(&g_serial_hw_lock);
}

void serial_hw_unlock_irqrestore(uint32_t flags) {
    spin_unlock_irqrestore(&g_serial_hw_lock, flags);
}

