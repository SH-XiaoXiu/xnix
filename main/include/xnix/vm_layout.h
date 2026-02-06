#ifndef XNIX_VM_LAYOUT_H
#define XNIX_VM_LAYOUT_H

#include <arch/mmu.h>

#define USER_ADDR_MAX   KERNEL_VIRT_BASE
#define USER_STACK_TOP  (KERNEL_VIRT_BASE - PAGE_SIZE)
#define USER_STACK_SIZE (64u * 1024u)

#endif /* XNIX_VM_LAYOUT_H */
