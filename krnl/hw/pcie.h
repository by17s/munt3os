#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
} __attribute__((packed)) pci_device_header_t;

#define MAX_PCIE_DEVICES 128

typedef struct {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint64_t ecam_base;
    pci_device_header_t header;
} pcie_device_info_t;

extern pcie_device_info_t pcie_devices[MAX_PCIE_DEVICES];
extern int pcie_device_count;

void pcie_init(void);
const char* pcie_get_class_name(uint8_t class_code);
bool pcie_enable_msi(pcie_device_info_t* dev, uint8_t vector);
