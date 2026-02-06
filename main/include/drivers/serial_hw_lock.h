#ifndef DRIVERS_SERIAL_HW_LOCK_H
#define DRIVERS_SERIAL_HW_LOCK_H

#include <xnix/types.h>

uint32_t serial_hw_lock_irqsave(void);
void     serial_hw_unlock_irqrestore(uint32_t flags);

#endif /* DRIVERS_SERIAL_HW_LOCK_H */
