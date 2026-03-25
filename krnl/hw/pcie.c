#include "pcie.h"
#include "acpi.h"
#include "log.h"
#include "sata/ahci.h"
#include <stddef.h>
#include <cstdlib.h>

extern void* phys_to_virt(uint64_t phys_addr);

LOG_MODULE("pcie")

static inline uint16_t pcie_read16(uint64_t base, uint32_t offset) {
    return *(volatile uint16_t*)(base + offset);
}

static inline uint32_t pcie_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t*)(base + offset);
}

static inline void pcie_write16(uint64_t base, uint32_t offset, uint16_t val) {
    *(volatile uint16_t*)(base + offset) = val;
}

static inline void pcie_write32(uint64_t base, uint32_t offset, uint32_t val) {
    *(volatile uint32_t*)(base + offset) = val;
}

pcie_device_info_t pcie_devices[MAX_PCIE_DEVICES];
int pcie_device_count = 0;

const char* pcie_get_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage Controller";
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x05: return "Memory Controller";
        case 0x06: return "Bridge";
        case 0x07: return "Simple Communication Controller";
        case 0x08: return "Base System Peripheral";
        case 0x09: return "Input Device Controller";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus Controller";
        case 0x0D: return "Wireless Controller";
        case 0x0E: return "Intelligent Controller";
        case 0x0F: return "Satellite Communication Controller";
        case 0x10: return "Encryption Controller";
        case 0x11: return "Signal Processing Controller";
        case 0x12: return "Processing Accelerator";
        case 0x13: return "Non-Essential Instrumentation";
        case 0x40: return "Coprocessor";
        case 0xFF: return "Unassigned Class";
        default: return "Unknown Class";
    }
}

bool pcie_enable_msi(pcie_device_info_t* dev, uint8_t vector) {
    if (!(dev->header.status & 0x0010)) { 
        return false;
    }

    uint64_t base_addr = dev->ecam_base;
    uint8_t cap_ptr = *(volatile uint8_t*)(base_addr + 0x34) & 0xFC; 

    while (cap_ptr != 0) {
        uint8_t cap_id = *(volatile uint8_t*)(base_addr + cap_ptr);
        if (cap_id == 0x05) { 
            uint16_t msg_ctrl = pcie_read16(base_addr, cap_ptr + 2);

            
            
            uint32_t msg_addr = 0xFEE00000;
            pcie_write32(base_addr, cap_ptr + 4, msg_addr);

            int data_offset = 8;
            if (msg_ctrl & (1 << 7)) { 
                pcie_write32(base_addr, cap_ptr + 8, 0); 
                data_offset = 12;
            }

            
            
            uint16_t msg_data = vector & 0xFF;
            pcie_write16(base_addr, cap_ptr + data_offset, msg_data);

            
            msg_ctrl |= 1;
            pcie_write16(base_addr, cap_ptr + 2, msg_ctrl);

            LOG_INFO("PCIe MSI enabled for %02x:%02x.%d on vector 0x%02x", dev->bus, dev->dev, dev->func, vector);
            return true;
        }
        cap_ptr = *(volatile uint8_t*)(base_addr + cap_ptr + 1) & 0xFC;
    }
    return false;
}

void pcie_init(void) {
    acpi_mcfg_t* mcfg = acpi_get_mcfg();
    if (!mcfg) {
        LOG_WARN("PCIe: MCFG table not found, cannot use ECAM.");
        return;
    }

    size_t entries_count = (mcfg->h.length - sizeof(acpi_mcfg_t)) / sizeof(mcfg_entry_t);
    LOG_INFO("PCIe: Initialization started. MCFG entries: %llu", entries_count);

    for (size_t i = 0; i < entries_count; i++) {
        mcfg_entry_t* entry = &mcfg->entries[i];

        LOG_INFO("PCIe: Segment %d, Bus %d-%d, Base 0x%llx", 
                 entry->pci_segment_group, entry->start_bus_number, entry->end_bus_number, entry->base_address);

        for (uint16_t bus = entry->start_bus_number; bus <= entry->end_bus_number; bus++) {
            for (uint8_t dev = 0; dev < 32; dev++) {
                for (uint8_t func = 0; func < 8; func++) {
                    
                    uint64_t offset = ((uint64_t)bus - entry->start_bus_number) << 20 | 
                                      (uint64_t)dev << 15 | 
                                      (uint64_t)func << 12;
                    
                    uint64_t phys_addr = entry->base_address + offset;
                    pci_device_header_t* pci_dev = (pci_device_header_t*)phys_to_virt(phys_addr);

                    if (pci_dev->vendor_id == 0xFFFF) continue; 

                    if (pcie_device_count < MAX_PCIE_DEVICES) {
                        pcie_device_info_t* info = &pcie_devices[pcie_device_count++];
                        info->bus = bus;
                        info->dev = dev;
                        info->func = func;
                        info->ecam_base = (uint64_t)pci_dev;
                        info->header = *pci_dev;
                        memcpy(&info->header, pci_dev, sizeof(pci_device_header_t));
                    }

                    LOG_INFO("  [PCIe] %02x:%02x.%d | Vendor:0x%04x Device:0x%04x | Class:0x%02x Sub:0x%02x ProgIF:0x%02x",
                             bus, dev, func, 
                             pci_dev->vendor_id, pci_dev->device_id, 
                             pci_dev->class_code, pci_dev->subclass, pci_dev->prog_if);

                    
                    if (pci_dev->class_code == 0x01 && pci_dev->subclass == 0x06) {
                        if (pci_dev->prog_if == 0x01) { 
                            pcie_device_info_t tmp = { 0 };
                            tmp.bus = (uint8_t)bus;
                            tmp.dev = (uint8_t)dev;
                            tmp.func = (uint8_t)func;
                            tmp.ecam_base = (uint64_t)pci_dev;
                            tmp.header = *pci_dev;
                            ahci_init(&tmp);
                        }
                    }

                    
                    if (pci_dev->status & 0x0010) {
                        LOG_INFO("  [PCIe] Capabilities detected, attempting MSI...");
                    }
                }
            }
        }
    }
}
