#include "vmm.h"
#include "pmm.h"
#include "../log.h"
#include "../cstdlib.h"
#include "../mm.h"

LOG_MODULE("vmm")

extern void* phys_to_virt(uint64_t phys_addr);

static vmm_context_t kernel_context;
vmm_context_t* current_vmm_context = NULL;

static inline void set_cr3(uint64_t pml4) {
    asm volatile("mov %0, %%cr3" : : "r"(pml4) : "memory");
}

static inline uint64_t get_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

vmm_context_t vmm_get_active_context(void) {
    uint64_t cr3 = get_cr3();
    vmm_context_t ctx;
    ctx.pml4_phys = (pagemap_t*)(cr3 & VMM_PHYS_MASK);
    ctx.pml4_virt = (pagemap_t*)phys_to_virt((uint64_t)ctx.pml4_phys);
    return ctx;
}

static pagemap_t* get_next_level(pagemap_t* current_level, int index, bool allocate, uint64_t flags) {
    if (current_level->entries[index] & VMM_FLAG_PRESENT) {
        uint64_t phys = current_level->entries[index] & VMM_PHYS_MASK;
        return (pagemap_t*)phys_to_virt(phys);
    }
    
    if (!allocate) return NULL;

    void* new_page = pmm_alloc(1);
    if (!new_page) return NULL;

    
    pagemap_t* virt = (pagemap_t*)phys_to_virt((uint64_t)new_page);
    for (int i = 0; i < 512; i++) virt->entries[i] = 0;

    
    
    current_level->entries[index] = ((uint64_t)new_page) | VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER;
    
    return virt;
}

bool vmm_map(vmm_context_t* ctx, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint16_t pml4_index = (virt >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt >> 30) & 0x1FF;
    uint16_t pd_index   = (virt >> 21) & 0x1FF;
    uint16_t pt_index   = (virt >> 12) & 0x1FF;

    
    uint64_t dir_flags = VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_PRESENT;

    pagemap_t* pdpt = get_next_level(ctx->pml4_virt, pml4_index, true, dir_flags);
    if (!pdpt) return false;

    pagemap_t* pd = get_next_level(pdpt, pdpt_index, true, dir_flags);
    if (!pd) return false;

    pagemap_t* pt = get_next_level(pd, pd_index, true, dir_flags);
    if (!pt) return false;

    pt->entries[pt_index] = (phys & VMM_PHYS_MASK) | flags | VMM_FLAG_PRESENT;
    
    
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");

    return true;
}

bool vmm_unmap(vmm_context_t* ctx, uint64_t virt) {
    uint16_t pml4_index = (virt >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt >> 30) & 0x1FF;
    uint16_t pd_index   = (virt >> 21) & 0x1FF;
    uint16_t pt_index   = (virt >> 12) & 0x1FF;

    pagemap_t* pdpt = get_next_level(ctx->pml4_virt, pml4_index, false, 0);
    if (!pdpt) return false;

    pagemap_t* pd = get_next_level(pdpt, pdpt_index, false, 0);
    if (!pd) return false;

    pagemap_t* pt = get_next_level(pd, pd_index, false, 0);
    if (!pt) return false;

    pt->entries[pt_index] = 0;
    
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

uint64_t vmm_virt_to_phys(vmm_context_t* ctx, uint64_t virt) {
    uint16_t pml4_index = (virt >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt >> 30) & 0x1FF;
    uint16_t pd_index   = (virt >> 21) & 0x1FF;
    uint16_t pt_index   = (virt >> 12) & 0x1FF;

    pagemap_t* pdpt = get_next_level(ctx->pml4_virt, pml4_index, false, 0);
    if (!pdpt) return 0;

    if (ctx->pml4_virt->entries[pml4_index] & VMM_FLAG_HUGE) {
        return (ctx->pml4_virt->entries[pml4_index] & VMM_PHYS_MASK) + (virt & 0x7FFFFFFFFF); 
    }

    pagemap_t* pd = get_next_level(pdpt, pdpt_index, false, 0);
    if (!pd) return 0;
    
    if (pdpt->entries[pdpt_index] & VMM_FLAG_HUGE) {
        
        return (pdpt->entries[pdpt_index] & 0x000FFFFFC0000000ull) + (virt & 0x3FFFFFFF);
    }

    pagemap_t* pt = get_next_level(pd, pd_index, false, 0);
    if (!pt) return 0;

    if (pd->entries[pd_index] & VMM_FLAG_HUGE) {
        
        return (pd->entries[pd_index] & 0x000FFFFFFFE00000ull) + (virt & 0x1FFFFF);
    }

    if (pt->entries[pt_index] & VMM_FLAG_PRESENT) {
        return (pt->entries[pt_index] & VMM_PHYS_MASK) + (virt & 0xFFF);
    }

    return 0;
}

void vmm_switch_context(vmm_context_t* ctx) {
    set_cr3((uint64_t)ctx->pml4_phys);
    current_vmm_context = ctx;
}

vmm_context_t* vmm_create_context(void) {
    vmm_context_t* ctx = (vmm_context_t*)kmalloc(sizeof(vmm_context_t));
    if (!ctx) return NULL;

    void* new_pml4_phys = pmm_alloc(1);
    if (!new_pml4_phys) {
        kfree(ctx);
        return NULL;
    }

    ctx->pml4_phys = (pagemap_t*)new_pml4_phys;
    ctx->pml4_virt = (pagemap_t*)phys_to_virt((uint64_t)new_pml4_phys);

    
    for (int i=0; i<512; i++) ctx->pml4_virt->entries[i] = 0;

    
    if (kernel_context.pml4_virt) {
        for (int i=256; i<512; i++) {
            ctx->pml4_virt->entries[i] = kernel_context.pml4_virt->entries[i];
        }
    }

    return ctx;
}

static void free_pagetable_hierarchy(pagemap_t* pt, int level) {
    for (int i=0; i<512; i++) {
        
        if (level == 1 && i >= 256) continue;
        
        if (pt->entries[i] & VMM_FLAG_PRESENT) {
            uint64_t phys = pt->entries[i] & VMM_PHYS_MASK;
            
            if (level < 4) {
                if ((pt->entries[i] & VMM_FLAG_HUGE) == 0) {
                    
                    free_pagetable_hierarchy((pagemap_t*)phys_to_virt(phys), level + 1);
                    pmm_free((void*)phys, 1); 
                } else {
                    
                    if ((pt->entries[i] & VMM_FLAG_NO_FREE) == 0) {
                        pmm_free((void*)phys, (level == 3) ? 512 : 512*512); 
                    }
                }
            } else {
                
                if ((pt->entries[i] & VMM_FLAG_NO_FREE) == 0) {
                    pmm_free((void*)phys, 1);
                }
            }
        }
    }
}

void vmm_destroy_context(vmm_context_t* ctx) {
    if (!ctx) return;
    free_pagetable_hierarchy(ctx->pml4_virt, 1);
    pmm_free(ctx->pml4_phys, 1);
    kfree(ctx);
}

void vmm_free_cr3(uint64_t cr3) {
    if (!cr3) return;
    pagemap_t* pml4_virt = (pagemap_t*)(cr3 + 0xFFFF800000000000);
    free_pagetable_hierarchy(pml4_virt, 1);
    pmm_free((void*)cr3, 1);
}

void vmm_init(void) {
    
    uint64_t boot_cr3 = get_cr3();
    
    kernel_context.pml4_phys = (pagemap_t*)(boot_cr3 & VMM_PHYS_MASK);
    kernel_context.pml4_virt = (pagemap_t*)phys_to_virt((uint64_t)kernel_context.pml4_phys);
    
    current_vmm_context = &kernel_context;
    
    LOG_INFO("VMM Initialization complete! Boot CR3: 0x%x", boot_cr3);
}

vmm_context_t* vmm_clone_context(vmm_context_t* parent) {
    if (!parent) return NULL;
    vmm_context_t* child = vmm_create_context();
    if (!child) return NULL;

    for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        if (!(parent->pml4_virt->entries[pml4_idx] & VMM_FLAG_PRESENT)) continue;
        
        pagemap_t* pdpt_parent = (pagemap_t*)phys_to_virt(parent->pml4_virt->entries[pml4_idx] & VMM_PHYS_MASK);
        pagemap_t* pdpt_child = get_next_level(child->pml4_virt, pml4_idx, true, VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_PRESENT);
        if (!pdpt_child) goto fail;

        for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
            if (!(pdpt_parent->entries[pdpt_idx] & VMM_FLAG_PRESENT)) continue;
            
            pagemap_t* pd_parent = (pagemap_t*)phys_to_virt(pdpt_parent->entries[pdpt_idx] & VMM_PHYS_MASK);
            pagemap_t* pd_child = get_next_level(pdpt_child, pdpt_idx, true, VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_PRESENT);
            if (!pd_child) goto fail;

            for (int pd_idx = 0; pd_idx < 512; pd_idx++) {
                if (!(pd_parent->entries[pd_idx] & VMM_FLAG_PRESENT)) continue;
                pagemap_t* pt_parent = (pagemap_t*)phys_to_virt(pd_parent->entries[pd_idx] & VMM_PHYS_MASK);
                pagemap_t* pt_child = get_next_level(pd_child, pd_idx, true, VMM_FLAG_WRITE | VMM_FLAG_USER | VMM_FLAG_PRESENT);
                if (!pt_child) goto fail;

                for (int pt_idx = 0; pt_idx < 512; pt_idx++) {
                    if (!(pt_parent->entries[pt_idx] & VMM_FLAG_PRESENT)) continue;
                    
                    void* new_page = pmm_alloc(1);
                    if (!new_page) goto fail;

                    uint64_t old_phys = pt_parent->entries[pt_idx] & VMM_PHYS_MASK;
                    memcpy((void*)phys_to_virt((uint64_t)new_page), (void*)phys_to_virt(old_phys), 4096);

                    pt_child->entries[pt_idx] = ((uint64_t)new_page) | (pt_parent->entries[pt_idx] & 0xFFF); 
                }
            }
        }
    }
    return child;
fail:
    vmm_destroy_context(child);
    return NULL;
}

vmm_context_t* vmm_get_kernel_context(void) {
    return &kernel_context;
}