#include "lapic.h"
#include "log.h"
#include <stddef.h>

LOG_MODULE("lapic");

extern void* phys_to_virt(uint64_t phys_addr);

static volatile uint32_t* lapic_base = NULL;

void lapic_init(uint64_t phys_addr) {
    lapic_base = (volatile uint32_t*)phys_to_virt(phys_addr);

    
    
    
    lapic_write(0xF0, lapic_read(0xF0) | 0x1FF); 

    LOG_INFO("LAPIC initialized at physical 0x%llx, virtual %p", phys_addr, lapic_base);
}

void lapic_timer_init(uint8_t vector, uint32_t initial_count, uint8_t divider) {
    
    
    
    lapic_write(0x3E0, divider);

    
    lapic_write(0x380, initial_count);

    
    
    lapic_write(0x320, 0x20000 | vector);

    LOG_INFO("LAPIC Timer configured: vector 0x%x, count %u", vector, initial_count);
}

void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base) return;
    volatile uint32_t* addr = (volatile uint32_t*)((uint8_t*)lapic_base + reg);
    *addr = val;
}

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    volatile uint32_t* addr = (volatile uint32_t*)((uint8_t*)lapic_base + reg);
    return *addr;
}

void lapic_eoi(void) {
    if (!lapic_base) return;
    
    lapic_write(0xB0, 0); 
}
