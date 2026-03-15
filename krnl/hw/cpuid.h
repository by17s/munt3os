#pragma once
#include <stdint.h>

typedef struct {
    char brand_string[49]; 
    uint32_t base_freq_mhz;
    uint32_t max_freq_mhz;
    uint32_t bus_freq_mhz;
} cpu_info_t;

static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    asm volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}


void get_cpu_info(cpu_info_t* info);