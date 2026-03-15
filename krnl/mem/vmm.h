#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#define VMM_FLAG_PRESENT       (1ull << 0)
#define VMM_FLAG_WRITE         (1ull << 1)
#define VMM_FLAG_USER          (1ull << 2)
#define VMM_FLAG_WRITE_THROUGH (1ull << 3)
#define VMM_FLAG_NO_CACHE      (1ull << 4)
#define VMM_FLAG_ACCESSED      (1ull << 5)
#define VMM_FLAG_DIRTY         (1ull << 6)
#define VMM_FLAG_HUGE          (1ull << 7)
#define VMM_FLAG_GLOBAL        (1ull << 8)
#define VMM_FLAG_NO_FREE       (1ull << 9)  
#define VMM_FLAG_NX            (1ull << 63)


#define VMM_PHYS_MASK 0x000FFFFFFFFFF000ull

typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(4096))) pagemap_t;


typedef struct {
    pagemap_t* pml4_phys;
    pagemap_t* pml4_virt;
} vmm_context_t;


void vmm_init(void);


vmm_context_t* vmm_create_context(void);


vmm_context_t* vmm_clone_context(vmm_context_t* parent);
void vmm_free_cr3(uint64_t cr3);


void vmm_destroy_context(vmm_context_t* ctx);


bool vmm_map(vmm_context_t* ctx, uint64_t virt, uint64_t phys, uint64_t flags);


bool vmm_unmap(vmm_context_t* ctx, uint64_t virt);


void vmm_switch_context(vmm_context_t* ctx);


vmm_context_t vmm_get_active_context(void);


uint64_t vmm_virt_to_phys(vmm_context_t* ctx, uint64_t virt);

vmm_context_t* vmm_get_kernel_context(void);