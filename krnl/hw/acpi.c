#include "acpi.h"
#include "lapic.h"

#include <memio.h>

#include "limine.h"
#include "log.h"
#include "mem/pmm.h"
#include "cstdlib.h"
#include "tty.h"

LOG_MODULE("acpi")

#define ACPI_DEVICES_PS2_KEYBOARD_PRESENT 0x01
#define ACPI_DEVICES_PS2_MOUSE_PRESENT 0x02

static int64_t acpi_devices_and_interfaces = 0;

int acpi_ps2_keyboard_present(void) {
    return (acpi_devices_and_interfaces & ACPI_DEVICES_PS2_KEYBOARD_PRESENT) != 0;
}

int acpi_ps2_mouse_present(void) {
    return (acpi_devices_and_interfaces & ACPI_DEVICES_PS2_MOUSE_PRESENT) != 0;
}

static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0
};

extern void* phys_to_virt(uint64_t phys_addr);

static acpi_fadt_t* fadt = NULL;


static void acpi_write_gas(acpi_gas_t* gas, uint32_t val) {
    if (gas->address == 0) return;

    if (gas->address_space == 0) { 
        volatile uint32_t* ptr = (volatile uint32_t*)phys_to_virt(gas->address);
        *ptr = val;
    } else if (gas->address_space == 1) { 
        if (gas->bit_width == 16) {
            asm volatile("outw %w0, %w1" : : "a"(val), "Nd"((uint16_t)gas->address));
        } else if (gas->bit_width == 32) {
            asm volatile("outl %0, %w1" : : "a"(val), "Nd"((uint16_t)gas->address));
        } else {
            asm volatile("outb %b0, %w1" : : "a"(val), "Nd"((uint16_t)gas->address));
        }
    }
}


static uint32_t acpi_read_gas(acpi_gas_t* gas) {
    if (gas->address == 0) return 0;

    if (gas->address_space == 0) { 
        volatile uint32_t* ptr = (volatile uint32_t*)phys_to_virt(gas->address);
        return *ptr;
    } else if (gas->address_space == 1) { 
        uint32_t val;
        if (gas->bit_width == 16) {
            asm volatile("inw %w1, %w0" : "=a"(val) : "Nd"((uint16_t)gas->address));
        } else if (gas->bit_width == 32) {
            asm volatile("inl %w1, %0" : "=a"(val) : "Nd"((uint16_t)gas->address));
        } else {
            asm volatile("inb %w1, %b0" : "=a"(val) : "Nd"((uint16_t)gas->address));
        }
        return val;
    }
    return 0;
}

void parse_fadt(acpi_sdt_header_t* header) {
    fadt = (acpi_fadt_t*)header;
    LOG_INFO("ACPI: FADT detected. Revision: %d", fadt->h.revision);

    
    uint64_t dsdt_addr = (fadt->h.revision >= 2 && fadt->x_dsdt != 0) ? fadt->x_dsdt : fadt->dsdt;
    LOG_INFO("  -> DSDT address: 0x%llx", dsdt_addr);

    
    if (fadt->boot_architecture_flags & (1 << 0)) {
        acpi_devices_and_interfaces |= (ACPI_DEVICES_PS2_KEYBOARD_PRESENT | ACPI_DEVICES_PS2_MOUSE_PRESENT);
        LOG_INFO("  -> Legacy devices: PS/2 Keyboard/Mouse present");
    }
    if (fadt->boot_architecture_flags & (1 << 1)) {
        acpi_devices_and_interfaces |= ACPI_DEVICES_PS2_KEYBOARD_PRESENT;
        LOG_INFO("  -> 8042 (KBC) present at 0x60/0x64");
    } 
    
    
    uint32_t pm1a_cnt;
    if (fadt->h.revision >= 2 && fadt->x_pm1a_control_block.address != 0) {
        pm1a_cnt = acpi_read_gas(&fadt->x_pm1a_control_block);
    } else {
        asm volatile("inw %w1, %w0" : "=a"(pm1a_cnt) : "Nd"((uint16_t)fadt->pm1a_control_block));
    }

    if (!(pm1a_cnt & 1)) { 
        if (fadt->smi_command_port != 0 && fadt->acpi_enable != 0) {
            LOG_INFO("ACPI: Transitioning to ACPI mode via SMI...");
            asm volatile("outb %b0, %w1" : : "a"(fadt->acpi_enable), "Nd"((uint16_t)fadt->smi_command_port));
            
            
            for (int i = 0; i < 1000; i++) {
                if (fadt->h.revision >= 2 && fadt->x_pm1a_control_block.address != 0)
                    pm1a_cnt = acpi_read_gas(&fadt->x_pm1a_control_block);
                else
                    asm volatile("inw %w1, %w0" : "=a"(pm1a_cnt) : "Nd"((uint16_t)fadt->pm1a_control_block));

                if (pm1a_cnt & 1) break;
            }
        }
    }
    
    if (pm1a_cnt & 1) LOG_INFO("ACPI: Hardware is in ACPI mode");
    else LOG_WARN("ACPI: Failed to enter ACPI mode");
}

static void parse_madt(acpi_sdt_header_t* header) {
    acpi_madt_t* madt = (acpi_madt_t*)header;
    
    
    LOG_INFO("ACPI: Found MADT (APIC). Local APIC Base: 0x%08x", madt->local_apic_address);

    
    lapic_init(madt->local_apic_address);

    
    uint8_t* ptr = (uint8_t*)madt + sizeof(acpi_madt_t);
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    int cpu_count = 0;
    int io_apic_count = 0;

    while (ptr < end) {
        madt_record_header_t* record = (madt_record_header_t*)ptr;

        if (record->type == 0) { 
            
            madt_local_apic_t* lapic = (madt_local_apic_t*)record;
            
            
            if ((lapic->flags & 1) || ((lapic->flags >> 1) & 1)) {
                if (acpi_cpu_count < MAX_CPUS) {
                    acpi_cpus[acpi_cpu_count].acpi_processor_id = lapic->acpi_processor_id;
                    acpi_cpus[acpi_cpu_count].apic_id = lapic->apic_id;
                    acpi_cpus[acpi_cpu_count].flags = lapic->flags;
                    acpi_cpus[acpi_cpu_count].is_bsp = (cpu_count == 0); 
                    acpi_cpu_count++;
                }
                cpu_count++;
                LOG_INFO("  ---> CPU Core Found: APIC ID %d", lapic->apic_id);
            }
        } else if (record->type == 1) { 
            
            madt_io_apic_t* ioapic = (madt_io_apic_t*)record;
            io_apic_count++;
            LOG_INFO("  ---> I/O APIC Found: ID %d, Address: 0x%0x", ioapic->io_apic_id, ioapic->io_apic_address);
        }

        
        ptr += record->length;
    }

    LOG_INFO("  ---> Total Enabled CPU Cores: %d", cpu_count);
    LOG_INFO("  ---> Total I/O APICs: %d", io_apic_count);
}

static acpi_mcfg_t* cached_mcfg = NULL;

void parse_mcfg(acpi_sdt_header_t* header) {
    cached_mcfg = (acpi_mcfg_t*)header;
    LOG_INFO("ACPI: MCFG table cached for later PCIe initialization.");
}

acpi_mcfg_t* acpi_get_mcfg(void) {
    return cached_mcfg;
}

acpi_cpu_t acpi_cpus[MAX_CPUS];
int acpi_cpu_count = 0;

void acpi_cpu_dump(void) {
    tty_printf("ACPI: Found %d CPUs\n", acpi_cpu_count);
    for (int i = 0; i < acpi_cpu_count; i++) {
        tty_printf("  CPU %d: APIC ID %d (BSP: %s)\n", 
                   i, 
                   acpi_cpus[i].apic_id, 
                   acpi_cpus[i].is_bsp ? "yes" : "no");
    }
}

void acpi_init(void) {
    if (rsdp_request.response == NULL) {
        LOG_ERROR("RSDP not found!");
        return;
    }

    acpi_devices_and_interfaces = 0; 

    
    acpi_rsdp_t* rsdp = (acpi_rsdp_t*)rsdp_request.response->address;

    LOG_INFO("ACPI: RSDP found (rev: %d)", rsdp->revision);

    acpi_sdt_header_t* xsdt_or_rsdt;
    int entries_count;
    uint64_t* xsdt_entries = NULL;
    uint32_t* rsdt_entries = NULL;
    
    
    bool use_xsdt = (rsdp->revision >= 2 && rsdp->xsdt_address != 0);

    if (use_xsdt) {
        
        xsdt_or_rsdt = (acpi_sdt_header_t*)phys_to_virt(rsdp->xsdt_address);
        
        
        entries_count = (xsdt_or_rsdt->length - sizeof(acpi_sdt_header_t)) / 8;
        xsdt_entries = (uint64_t*)((uint8_t*)xsdt_or_rsdt + sizeof(acpi_sdt_header_t));
        
        LOG_INFO("Using XSDT with %d entries", entries_count);
    } else {
        
        xsdt_or_rsdt = (acpi_sdt_header_t*)phys_to_virt(rsdp->rsdt_address);
        entries_count = (xsdt_or_rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
        rsdt_entries = (uint32_t*)((uint8_t*)xsdt_or_rsdt + sizeof(acpi_sdt_header_t));
        
        LOG_INFO("Using RSDT. Found tables: %d", entries_count);
    }

    
    for (int i = 0; i < entries_count; i++) {
        uint64_t table_phys;
        if (use_xsdt) {
            table_phys = xsdt_entries[i];
        } else {
            table_phys = rsdt_entries[i];
        }

        acpi_sdt_header_t* table = (acpi_sdt_header_t*)phys_to_virt(table_phys);

        
        char sig[5] = {0};
        for (int j = 0; j < 4; j++) {
            sig[j] = table->signature[j];
        }

        LOG_INFO("  -> ACPI Table: [%s], Size: %d bytes", sig, table->length);

        if          (memcmp(sig, "APIC", 4) == 0) {
            parse_madt(table);
        } else if   (memcmp(sig, "FACP", 4) == 0) {
            parse_fadt(table);
        } else if   (memcmp(sig, "MCFG", 4) == 0) {
            parse_mcfg(table);
        }
    }
}