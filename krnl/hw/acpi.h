#pragma once

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MSI_OHCI_VECTOR 0x40


typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;      
    uint32_t length;            
    uint64_t xsdt_address;      
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;


typedef struct {
    char signature[4];          
    uint32_t length;            
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;


typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_address; 
    uint32_t flags;              
} __attribute__((packed)) acpi_madt_t;


typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) madt_record_header_t;


typedef struct {
    madt_record_header_t header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags; 
} __attribute__((packed)) madt_local_apic_t;


typedef struct {
    madt_record_header_t header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) madt_io_apic_t;

typedef struct {
    uint8_t  address_space;
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed)) acpi_gas_t;

typedef struct {
    acpi_sdt_header_t h;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved;
    uint8_t  preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pmtimer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t  pm1_event_length;
    uint8_t  pm1_control_length;
    uint8_t  pm2_control_length;
    uint8_t  pm_timer_length;
    uint8_t  gpe0_length;
    uint8_t  gpe1_length;
    uint8_t  gpe1_base;
    uint8_t  cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alarm;
    uint8_t  month_alarm;
    uint8_t  century;
    uint16_t boot_architecture_flags;
    uint8_t  reserved2;
    uint32_t flags;
    acpi_gas_t reset_reg;
    uint8_t  reset_value;
    uint8_t  reserved3[3];
    uint64_t x_firmware_control;
    uint64_t x_dsdt;
    acpi_gas_t x_pm1a_event_block;
    acpi_gas_t x_pm1b_event_block;
    acpi_gas_t x_pm1a_control_block;
    acpi_gas_t x_pm1b_control_block;
    acpi_gas_t x_pm2_control_block;
    acpi_gas_t x_pm_timer_block;
    acpi_gas_t x_gpe0_block;
    acpi_gas_t x_gpe1_block;
} __attribute__((packed)) acpi_fadt_t;

typedef struct {
    uint64_t base_address;      
    uint16_t pci_segment_group; 
    uint8_t  start_bus_number;  
    uint8_t  end_bus_number;    
    uint32_t reserved;
} __attribute__((packed)) mcfg_entry_t;

typedef struct {
    acpi_sdt_header_t h;
    uint64_t reserved;
    mcfg_entry_t entries[];     
} __attribute__((packed)) acpi_mcfg_t;

acpi_mcfg_t* acpi_get_mcfg(void);



#define MAX_CPUS 256

typedef struct {
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
    bool is_bsp;
} acpi_cpu_t;

extern acpi_cpu_t acpi_cpus[MAX_CPUS];
extern int acpi_cpu_count;

int acpi_ps2_keyboard_present(void);
int acpi_ps2_mouse_present(void);

void acpi_init(void);
void acpi_cpu_dump(void);