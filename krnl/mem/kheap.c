#include "kheap.h"
#include "pmm.h"
#include "cstdlib.h"

#define HEAP_MAGIC 0xC001C0DE
#define MIN_BLOCK_SIZE 32
#define PAGE_SIZE 4096

typedef struct kheap_block {
    uint32_t magic;
    bool is_free;
    size_t size;
    struct kheap_block* next;
    struct kheap_block* prev;
} kheap_block_t;

static kheap_block_t* heap_start = NULL;
static kheap_stat_t stats = {0};


extern void* phys_to_virt(uint64_t phys_addr);


void kheap_init(size_t initial_pages) {
    if (initial_pages == 0) initial_pages = 4096;
    
    void* phys_addr = pmm_alloc(initial_pages);
    if (!phys_addr) return;

    heap_start = (kheap_block_t*)phys_to_virt((uint64_t)phys_addr);
    stats.total_size = initial_pages * PAGE_SIZE;
    
    heap_start->magic = HEAP_MAGIC;
    heap_start->is_free = true;
    heap_start->size = stats.total_size - sizeof(kheap_block_t);
    heap_start->next = NULL;
    heap_start->prev = NULL;

    stats.free_size = heap_start->size;
    stats.used_size = sizeof(kheap_block_t);
    stats.block_count = 1;
    stats.initialized = true;
}


static void split_block(kheap_block_t* block, size_t size) {
    if (block->size <= size + sizeof(kheap_block_t) + MIN_BLOCK_SIZE) return;

    kheap_block_t* new_block = (kheap_block_t*)((uint8_t*)block + sizeof(kheap_block_t) + size);
    new_block->magic = HEAP_MAGIC;
    new_block->is_free = true;
    new_block->size = block->size - size - sizeof(kheap_block_t);
    new_block->next = block->next;
    new_block->prev = block;
    
    if (new_block->next) new_block->next->prev = new_block;
    block->next = new_block;
    block->size = size;

    stats.block_count++;
    stats.used_size += sizeof(kheap_block_t);
    stats.free_size -= sizeof(kheap_block_t);
}


void* khmalloc(size_t size) {
    if (!stats.initialized || size == 0) return NULL;

    size = (size + 7) & ~7;
    size_t total_required = size + sizeof(kheap_block_t*);
    kheap_block_t* current = heap_start;

    while (current) {
        if (current->is_free && current->size >= total_required) {
            split_block(current, total_required);
            
            current->is_free = false;
            stats.free_size -= current->size;
            stats.used_size += current->size;

            uintptr_t payload_addr = (uintptr_t)current + sizeof(kheap_block_t);
            payload_addr += sizeof(kheap_block_t*);
            ((kheap_block_t**)payload_addr)[-1] = current;

            return (void*)payload_addr;
        }
        current = current->next;
    }

    return NULL;
}


void* khaligned_alloc(size_t alignment, size_t size) {
    if (!stats.initialized || size == 0 || alignment == 0) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;

    size_t total_required = size + alignment + sizeof(kheap_block_t*);
    void* raw_ptr = khmalloc(total_required);
    if (!raw_ptr) return NULL;

    kheap_block_t* block = ((kheap_block_t**)raw_ptr)[-1];
    uintptr_t raw_addr = (uintptr_t)block + sizeof(kheap_block_t) + sizeof(kheap_block_t*);
    uintptr_t aligned_addr = (raw_addr + alignment - 1) & ~(alignment - 1);
    ((kheap_block_t**)aligned_addr)[-1] = block;

    return (void*)aligned_addr;
}


void* khcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = khmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}


void* khrealloc(void* ptr, size_t new_size) {
    if (!ptr) return khmalloc(new_size);
    if (new_size == 0) {
        khfree(ptr);
        return NULL;
    }

    kheap_block_t* block = ((kheap_block_t**)ptr)[-1];
    if (block->size >= new_size + sizeof(kheap_block_t*)) return ptr;

    void* new_ptr = khmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size - sizeof(kheap_block_t*));
        khfree(ptr);
    }
    
    return new_ptr;
}


void khfree(void* ptr) {
    if (!ptr || !stats.initialized) return;

    kheap_block_t* block = ((kheap_block_t**)ptr)[-1];
    if (block->magic != HEAP_MAGIC || block->is_free) return;

    block->is_free = true;
    stats.free_size += block->size;
    stats.used_size -= block->size;

    if (block->next && block->next->is_free) {
        stats.block_count--;
        stats.free_size += sizeof(kheap_block_t);
        stats.used_size -= sizeof(kheap_block_t);
        
        block->size += sizeof(kheap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    if (block->prev && block->prev->is_free) {
        kheap_block_t* prev = block->prev;
        stats.block_count--;
        stats.free_size += sizeof(kheap_block_t);
        stats.used_size -= sizeof(kheap_block_t);

        prev->size += sizeof(kheap_block_t) + block->size;
        prev->next = block->next;
        if (block->next) block->next->prev = prev;
    }
}


bool kheap_is_free(void* ptr) {
    if (!ptr || !stats.initialized) return false;
    
    kheap_block_t* block = ((kheap_block_t**)ptr)[-1];
    if (block->magic != HEAP_MAGIC) return false;
    
    return block->is_free;
}


kheap_stat_t kheap_get_stat(void) {
    return stats;
}