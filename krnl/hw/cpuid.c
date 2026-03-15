#include "cpuid.h"
#include <stddef.h>

void get_cpu_info(cpu_info_t* info) {
    if (!info) return;

    uint32_t eax, ebx, ecx, edx;

    
    
    cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t* ptr = (uint32_t*)info->brand_string;
        
        
        cpuid(0x80000002, &ptr[0], &ptr[1], &ptr[2], &ptr[3]);
        
        cpuid(0x80000003, &ptr[4], &ptr[5], &ptr[6], &ptr[7]);
        
        cpuid(0x80000004, &ptr[8], &ptr[9], &ptr[10], &ptr[11]);
        
        info->brand_string[48] = '\0'; 
    } else {
        
        info->brand_string[0] = '\0';
    }

    
    info->base_freq_mhz = 0;
    info->max_freq_mhz = 0;
    info->bus_freq_mhz = 0;

    
    cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x16) {
        cpuid(0x16, &eax, &ebx, &ecx, &edx);
        
        
        info->base_freq_mhz = eax & 0xFFFF;
        
        info->max_freq_mhz = ebx & 0xFFFF;
        
        info->bus_freq_mhz = ecx & 0xFFFF;
    }
}