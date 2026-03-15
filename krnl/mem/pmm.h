#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096


struct pmm_stat {
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t usable_pages;
    uint64_t total_pages;
    bool initialized;
};


void    pmm_init(void);
void*   pmm_alloc(uint64_t count);
void    pmm_free(void* ptr, uint64_t count);
void    pmm_stat(struct pmm_stat *stat);