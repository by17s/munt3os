#include "pmm.h"
#include "limine.h"

#include "log.h"
LOG_MODULE("pmm");

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

static uint8_t* bitmap = NULL;
static uint64_t bitmap_size_bytes = 0;
static uint64_t hhdm_offset = 0;
static struct pmm_stat stats = {0, 0, 0, false};
static uint64_t last_scanned_bit = 0; 

#define BITMAP_SET(bit)   (bitmap[(bit) / 8] |=  (1 << ((bit) % 8)))
#define BITMAP_CLEAR(bit) (bitmap[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BITMAP_TEST(bit)  (bitmap[(bit) / 8] &   (1 << ((bit) % 8)))

void* phys_to_virt(uint64_t phys_addr) {
    return (void*)(phys_addr + hhdm_offset);
}




void pmm_init(void) {
    if (memmap_request.response == NULL || hhdm_request.response == NULL) {
        
        LOG_ERROR("Failed to initialize PMM: missing memmap or HHDM");
        return;
    }

    hhdm_offset = hhdm_request.response->offset;
    struct limine_memmap_response* mmap = memmap_request.response;

    
    uint64_t highest_addr = 0;
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        uint64_t top = mmap->entries[i]->base + mmap->entries[i]->length;
        if (top > highest_addr) {
            highest_addr = top;
        }
    }

    stats.total_pages = highest_addr / PAGE_SIZE;
    bitmap_size_bytes = stats.total_pages / 8;
    if (bitmap_size_bytes * 8 < stats.total_pages) bitmap_size_bytes++; 

    
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size_bytes) {
            
            bitmap = (uint8_t*)phys_to_virt(entry->base);
            break;
        }
    }

    if (bitmap == NULL) {
        LOG_ERROR("Failed to initialize PMM: no space for bitmap");
        return;
    }

    
    for (uint64_t i = 0; i < bitmap_size_bytes; i++) {
        bitmap[i] = 0xFF;
    }

    
    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry* entry = mmap->entries[i];
        LOG_INFO("Memmap entry: base=0x%p, length=0x%p, type=%lu", 
            entry->base, entry->length, entry->type);
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE) {
                uint64_t bit_idx = (entry->base + j) / PAGE_SIZE;
                BITMAP_CLEAR(bit_idx);
                stats.free_pages++;
            }
        }
    }

    
    uint64_t bitmap_phys_base = (uint64_t)bitmap - hhdm_offset;
    for (uint64_t i = 0; i < bitmap_size_bytes; i += PAGE_SIZE) {
        uint64_t bit_idx = (bitmap_phys_base + i) / PAGE_SIZE;
        BITMAP_SET(bit_idx);
        stats.free_pages--;
    }

    stats.used_pages = 0;
    stats.initialized = true;
    LOG_INFO("PMM initialized: total=%llu pages, free=%llu pages, used=%llu pages", 
        stats.total_pages, stats.free_pages, stats.used_pages);

    stats.usable_pages = stats.free_pages;
}


void* pmm_alloc(uint64_t count) {
    if (!stats.initialized || count == 0) return NULL;

    uint64_t start_scan_bit = last_scanned_bit;
    uint64_t free_streak = 0;     
    uint64_t streak_start = 0;    

    
    for (uint64_t i = 0; i < stats.total_pages; i++) {
        uint64_t bit_idx = (start_scan_bit + i) % stats.total_pages;

        
        
        
        if (bit_idx == 0 && free_streak > 0) {
            free_streak = 0;
        }

        if (!BITMAP_TEST(bit_idx)) {
            
            if (free_streak == 0) {
                streak_start = bit_idx; 
            }
            free_streak++;

            
            if (free_streak == count) {
                
                for (uint64_t j = 0; j < count; j++) {
                    BITMAP_SET(streak_start + j);
                }
                
                
                last_scanned_bit = (streak_start + count) % stats.total_pages;
                
                stats.free_pages -= count;
                stats.used_pages += count;
                
                
                return (void*)(streak_start * PAGE_SIZE);
            }
        } else {
            
            free_streak = 0;
        }
    }

    
    LOG_WARN("Allocation failed: %llu contiguous free pages not available", count);
    return NULL; 
}

void pmm_free(void* ptr, uint64_t count) {
    if (!stats.initialized || ptr == NULL || count == 0) return;

    uint64_t phys_addr = (uint64_t)ptr;
    uint64_t start_bit = phys_addr / PAGE_SIZE;

    
    for (uint64_t i = 0; i < count; i++) {
        uint64_t bit_idx = start_bit + i;

        
        if (BITMAP_TEST(bit_idx)) {
            BITMAP_CLEAR(bit_idx);
            stats.free_pages++;
            stats.used_pages--;
        } else {
            LOG_WARN("Double free detected at physical address: 0x%llx", (bit_idx * PAGE_SIZE));
        }
    }
}


void pmm_stat(struct pmm_stat *stat) {
    if(stat == NULL) 
        return;
    *stat = stats;
    return;
}