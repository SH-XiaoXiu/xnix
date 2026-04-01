#include <asm/apic.h>
#include <asm/mmu.h>
#include <asm/smp_defs.h>
#include <xnix/string.h>
#include <xnix/vmm.h>

struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header hdr;
    uint32_t               lapic_addr;
    uint32_t               flags;
} __attribute__((packed));

struct acpi_madt_entry_hdr {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_lapic {
    struct acpi_madt_entry_hdr hdr;
    uint8_t                    acpi_processor_id;
    uint8_t                    apic_id;
    uint32_t                   flags;
} __attribute__((packed));

struct acpi_madt_ioapic {
    struct acpi_madt_entry_hdr hdr;
    uint8_t                    ioapic_id;
    uint8_t                    reserved;
    uint32_t                   addr;
    uint32_t                   gsi_base;
} __attribute__((packed));

static uint8_t acpi_checksum(const void *data, size_t len) {
    const uint8_t *p   = (const uint8_t *)data;
    uint8_t        sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += p[i];
    }
    return sum;
}

static void acpi_map_range(paddr_t phys, size_t len) {
    paddr_t start = phys & PAGE_MASK;
    paddr_t end   = (phys + len + PAGE_SIZE - 1) & PAGE_MASK;
    for (paddr_t p = start; p < end; p += PAGE_SIZE) {
        (void)vmm_map_page(NULL, p, p, VMM_PROT_READ | VMM_PROT_WRITE);
    }
}

static __attribute__((noinline)) uint16_t acpi_read_u16(paddr_t phys) {
    uint16_t v;
    memcpy(&v, (const void *)(uintptr_t)phys, sizeof(v));
    return v;
}

static const struct acpi_rsdp *acpi_find_rsdp_in_range(paddr_t start, paddr_t end) {
    acpi_map_range(start, end - start);
    for (paddr_t addr = start; addr < end; addr += 16) {
        const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)addr;
        if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
            continue;
        }
        if (acpi_checksum(rsdp, 20) != 0) {
            continue;
        }
        if (rsdp->revision >= 2 && rsdp->length >= 20) {
            acpi_map_range((paddr_t)addr, rsdp->length);
            if (acpi_checksum(rsdp, rsdp->length) != 0) {
                continue;
            }
        }
        return rsdp;
    }
    return NULL;
}

static const struct acpi_rsdp *acpi_find_rsdp(void) {
    const paddr_t ebda_ptr_addr  = 0x40E;
    const paddr_t bios_rom_start = 0xE0000;
    const paddr_t bios_rom_end   = 0x100000;

    acpi_map_range(ebda_ptr_addr, 2);
    uint16_t ebda_seg = acpi_read_u16(ebda_ptr_addr);
    if (ebda_seg) {
        paddr_t                 ebda_base = (paddr_t)ebda_seg << 4;
        const struct acpi_rsdp *rsdp      = acpi_find_rsdp_in_range(ebda_base, ebda_base + 1024);
        if (rsdp) {
            return rsdp;
        }
    }

    return acpi_find_rsdp_in_range(bios_rom_start, bios_rom_end);
}

static const struct acpi_sdt_header *acpi_map_sdt(paddr_t phys) {
    if (!phys) {
        return NULL;
    }
    acpi_map_range(phys, sizeof(struct acpi_sdt_header));
    const struct acpi_sdt_header *hdr = (const struct acpi_sdt_header *)phys;
    if (hdr->length < sizeof(struct acpi_sdt_header)) {
        return NULL;
    }
    acpi_map_range(phys, hdr->length);
    if (acpi_checksum(hdr, hdr->length) != 0) {
        return NULL;
    }
    return hdr;
}

static const struct acpi_madt *acpi_find_madt(const struct acpi_rsdp *rsdp) {
    if (!rsdp) {
        return NULL;
    }

    paddr_t sdt_phys = 0;
    bool    is_xsdt  = false;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        sdt_phys = (paddr_t)(uintptr_t)rsdp->xsdt_address;
        is_xsdt  = true;
    } else if (rsdp->rsdt_address) {
        sdt_phys = (paddr_t)rsdp->rsdt_address;
        is_xsdt  = false;
    } else {
        return NULL;
    }

    const struct acpi_sdt_header *sdt = acpi_map_sdt(sdt_phys);
    if (!sdt) {
        return NULL;
    }

    uint32_t entry_size = is_xsdt ? 8 : 4;
    if (sdt->length < sizeof(*sdt) + entry_size) {
        return NULL;
    }

    uint32_t       entry_count = (sdt->length - sizeof(*sdt)) / entry_size;
    const uint8_t *entries     = (const uint8_t *)(uintptr_t)(sdt_phys + sizeof(*sdt));

    for (uint32_t i = 0; i < entry_count; i++) {
        paddr_t table_phys;
        if (is_xsdt) {
            uint64_t v;
            memcpy(&v, entries + i * 8, 8);
            table_phys = (paddr_t)(uintptr_t)v;
        } else {
            uint32_t v;
            memcpy(&v, entries + i * 4, 4);
            table_phys = (paddr_t)v;
        }

        const struct acpi_sdt_header *hdr = acpi_map_sdt(table_phys);
        if (!hdr) {
            continue;
        }
        if (memcmp(hdr->signature, "APIC", 4) == 0) {
            return (const struct acpi_madt *)(uintptr_t)table_phys;
        }
    }

    return NULL;
}

int acpi_madt_parse(struct smp_info *info) {
    if (!info) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->cpu_count      = 1;
    info->bsp_id         = 0;
    info->lapic_base     = LAPIC_BASE_DEFAULT;
    info->ioapic_base    = IOAPIC_BASE_DEFAULT;
    info->apic_available = false;

    const struct acpi_rsdp *rsdp = acpi_find_rsdp();
    if (!rsdp) {
        return -1;
    }

    const struct acpi_madt *madt = acpi_find_madt(rsdp);
    if (!madt) {
        return -1;
    }

    acpi_map_range((paddr_t)(uintptr_t)madt, madt->hdr.length);

    info->lapic_base = (paddr_t)madt->lapic_addr ? (paddr_t)madt->lapic_addr : LAPIC_BASE_DEFAULT;
    info->apic_available = true;

    uint32_t cpu_count = 0;

    const uint8_t *p =
        (const uint8_t *)(uintptr_t)((paddr_t)(uintptr_t)madt + sizeof(struct acpi_madt));
    const uint8_t *end = (const uint8_t *)(uintptr_t)((paddr_t)(uintptr_t)madt + madt->hdr.length);

    while (p + sizeof(struct acpi_madt_entry_hdr) <= end) {
        const struct acpi_madt_entry_hdr *eh = (const struct acpi_madt_entry_hdr *)p;
        if (eh->length < sizeof(*eh) || p + eh->length > end) {
            break;
        }

        if (eh->type == 0 && eh->length >= sizeof(struct acpi_madt_lapic)) {
            const struct acpi_madt_lapic *lapic = (const struct acpi_madt_lapic *)p;
            if ((lapic->flags & 1) && cpu_count < CFG_MAX_CPUS) {
                info->lapic_ids[cpu_count++] = lapic->apic_id;
            }
        } else if (eh->type == 1 && eh->length >= sizeof(struct acpi_madt_ioapic)) {
            const struct acpi_madt_ioapic *ioapic = (const struct acpi_madt_ioapic *)p;
            info->ioapic_id                       = ioapic->ioapic_id;
            info->ioapic_base                     = (paddr_t)ioapic->addr;
        }

        p += eh->length;
    }

    if (cpu_count == 0) {
        info->cpu_count    = 1;
        info->lapic_ids[0] = 0;
        info->bsp_id       = 0;
        return 0;
    }

    info->cpu_count = cpu_count;

    extern uint8_t lapic_get_id(void);
    uint8_t        bsp_lapic_id = lapic_get_id();
    for (uint32_t i = 0; i < info->cpu_count; i++) {
        if (info->lapic_ids[i] == bsp_lapic_id) {
            info->bsp_id = i;
            break;
        }
    }

    return 0;
}
