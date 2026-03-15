#pragma once

#include <stdint.h>

void lapic_init(uint64_t phys_addr);
void lapic_timer_init(uint8_t vector, uint32_t initial_count, uint8_t divider);
void lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_read(uint32_t reg);
void lapic_eoi(void);
