#include "kbuddy.h"
#include "log.h"
LOG_MODULE("kbuddy")

static void list_add(kmem_buddy_block_t** list, kmem_buddy_block_t* block) {
    block->next = *list;
    block->prev = NULL;
    if (*list) (*list)->prev = block;
    *list = block;
}


static void list_remove(kmem_buddy_block_t** list, kmem_buddy_block_t* block) {
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;
    if (*list == block) *list = block->next;
    block->prev = NULL;
    block->next = NULL;
}


void kmem_buddy_init(kmem_buddy_allocator_t* alloc, void* pool_start, size_t pool_size) {
    alloc->pool_start = pool_start;
    alloc->pool_size = pool_size;
    for (int i = 0; i <= BUDDY_MAX_ORDER; i++) alloc->free_lists[i] = NULL;

    uintptr_t current_addr = (uintptr_t)pool_start;
    size_t remaining = pool_size;

    
    while (remaining >= (1ULL << BUDDY_MIN_SHIFT)) {
        uint8_t order = BUDDY_MAX_ORDER;
        
        while (order > 0) {
            size_t block_size = 1ULL << (BUDDY_MIN_SHIFT + order);
            
            if (remaining >= block_size && (current_addr & (block_size - 1)) == 0) {
                break;
            }
            order--;
        }

        kmem_buddy_block_t* block = (kmem_buddy_block_t*)current_addr;
        block->magic = BUDDY_MAGIC;
        block->is_free = true;
        block->order = order;
        
        list_add(&alloc->free_lists[order], block);

        size_t size_used = 1ULL << (BUDDY_MIN_SHIFT + order);
        current_addr += size_used;
        remaining -= size_used;
    }
    
    LOG_INFO("Initialized pool at 0x%p (Size: %llu bytes)", pool_start, pool_size);
}


void* kmem_buddy_alloc(kmem_buddy_allocator_t* alloc, size_t size) {
    if (size == 0) return NULL;

    size_t total_size = size + sizeof(kmem_buddy_block_t);
    uint8_t target_order = 0;
    
    
    while ((1ULL << (BUDDY_MIN_SHIFT + target_order)) < total_size && target_order <= BUDDY_MAX_ORDER) {
        target_order++;
    }

    if (target_order > BUDDY_MAX_ORDER) return NULL; 

    
    uint8_t order = target_order;
    while (order <= BUDDY_MAX_ORDER && alloc->free_lists[order] == NULL) {
        order++;
    }

    if (order > BUDDY_MAX_ORDER) {
        LOG_WARN("Out of memory!");
        return NULL;
    }

    
    kmem_buddy_block_t* block = alloc->free_lists[order];
    list_remove(&alloc->free_lists[order], block);

    
    while (order > target_order) {
        order--;
        size_t half_size = 1ULL << (BUDDY_MIN_SHIFT + order);
        
        
        kmem_buddy_block_t* buddy = (kmem_buddy_block_t*)((uint8_t*)block + half_size);
        buddy->magic = BUDDY_MAGIC;
        buddy->is_free = true;
        buddy->order = order;
        
        
        list_add(&alloc->free_lists[order], buddy);
        
        
        block->order = order;
    }

    block->is_free = false;
    return (void*)((uint8_t*)block + sizeof(kmem_buddy_block_t));
}


void kmem_buddy_free(kmem_buddy_allocator_t* alloc, void* ptr) {
    if (!ptr) return;

    kmem_buddy_block_t* block = (kmem_buddy_block_t*)((uint8_t*)ptr - sizeof(kmem_buddy_block_t));

    if (block->magic != BUDDY_MAGIC || block->is_free) {
        LOG_ERROR("Invalid pointer or double free!");
        return;
    }

    uint8_t order = block->order;

    
    while (order < BUDDY_MAX_ORDER) {
        size_t block_size = 1ULL << (BUDDY_MIN_SHIFT + order);
        
        
        uintptr_t rel_addr = (uintptr_t)block - (uintptr_t)alloc->pool_start;
        uintptr_t buddy_rel = rel_addr ^ block_size;
        kmem_buddy_block_t* buddy = (kmem_buddy_block_t*)((uint8_t*)alloc->pool_start + buddy_rel);

        
        if ((uintptr_t)buddy >= (uintptr_t)alloc->pool_start + alloc->pool_size) break;
        if (buddy->magic != BUDDY_MAGIC || !buddy->is_free || buddy->order != order) break;

        
        list_remove(&alloc->free_lists[order], buddy);

        
        if (buddy < block) block = buddy;

        order++;
        block->order = order; 
    }

    
    block->is_free = true;
    list_add(&alloc->free_lists[order], block);
}