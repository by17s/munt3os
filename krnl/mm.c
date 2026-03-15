#include <mm.h>

#include "mem/pmm.h"
#include "mem/kslab.h"
#include "mem/kbuddy.h"
#include "mem/kheap.h"

#include "cstdlib.h"

#include "log.h"
LOG_MODULE("mm");

enum {
    KMALLOC_32 = 0,
    KMALLOC_64,
    KMALLOC_96,
    KMALLOC_128,
    KMALLOC_196,
    KMALLOC_256,
    KMALLOC_512,
    KMALLOC_1024,
    KMALLOC_2048,
    KMALLOC_4096,

    KMALLOC_COUNT
};

static kmem_cache_t* kmallocs[KMALLOC_COUNT] = {};
static kmem_buddy_allocator_t kbuddy = {0};

extern void* phys_to_virt(uint64_t phys_addr);



void mm_test(void) {
    LOG_INFO("Starting memory allocator test...");

    
    void* ptr1 = kmalloc(25);   
    void* ptr2 = kmalloc(100);  
    
    LOG_INFO("kmalloc(25)   -> 0x%p", ptr1);
    LOG_INFO("kmalloc(100)  -> 0x%p", ptr2);

    
    void* ptr3 = kmalloc(4096);
    void* ptr4 = kmalloc(9000);
    
    LOG_INFO("kmalloc(4096) -> 0x%p", ptr3);
    LOG_INFO("kmalloc(9000) -> 0x%p", ptr4);

    
    if (ptr1) ((char*)ptr1)[0] = 'A';
    if (ptr3) ((char*)ptr3)[0] = 'B';

    
    LOG_INFO("Freeing memory...");
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    kfree(ptr4);

    LOG_INFO("Memory test completed successfully!");
}

int mm_init(void) {
    pmm_init();
    kheap_init(64); 
    
    kmallocs[KMALLOC_32]    = kmem_cache_create("kmalloc-32",   32);
    kmallocs[KMALLOC_64]    = kmem_cache_create("kmalloc-64",   64);
    kmallocs[KMALLOC_96]    = kmem_cache_create("kmalloc-96",   96);
    kmallocs[KMALLOC_128]   = kmem_cache_create("kmalloc-128",  128);
    kmallocs[KMALLOC_196]   = kmem_cache_create("kmalloc-196",  196);
    kmallocs[KMALLOC_256]   = kmem_cache_create("kmalloc-256",  256);
    kmallocs[KMALLOC_512]   = kmem_cache_create("kmalloc-512",  512);
    kmallocs[KMALLOC_1024]  = kmem_cache_create("kmalloc-1024", 1024);
    kmallocs[KMALLOC_2048]  = kmem_cache_create("kmalloc-2048", 2048);
    //kmallocs[KMALLOC_4096]  = kmem_cache_create("kmalloc-4096", 4096);

    
    #define BUDDY_POOL_PAGES 256
    void* buddy_phys = pmm_alloc(BUDDY_POOL_PAGES);
    if (buddy_phys == NULL) {
        LOG_ERROR("mm_init: pmm_alloc for buddy pool failed!");
        return -1;
    }
    kmem_buddy_init(
        &kbuddy,
        phys_to_virt((uint64_t)buddy_phys),
        BUDDY_POOL_PAGES * 4096
    );

    mm_test();

    return 0;
}

void* kmalloc(size_t size) {
    if (size == 0)      return NULL;
    if (size <= 32)     return kmem_cache_alloc(kmallocs[KMALLOC_32]);
    if (size <= 64)     return kmem_cache_alloc(kmallocs[KMALLOC_64]);
    if (size <= 96)     return kmem_cache_alloc(kmallocs[KMALLOC_96]);
    if (size <= 128)    return kmem_cache_alloc(kmallocs[KMALLOC_128]);
    if (size <= 196)    return kmem_cache_alloc(kmallocs[KMALLOC_196]);
    if (size <= 256)    return kmem_cache_alloc(kmallocs[KMALLOC_256]);
    if (size <= 512)    return kmem_cache_alloc(kmallocs[KMALLOC_512]);
    if (size <= 1024)   return kmem_cache_alloc(kmallocs[KMALLOC_1024]);
    if (size <= 2048)   return kmem_cache_alloc(kmallocs[KMALLOC_2048]);
    //if (size <= 4096 && kmallocs[KMALLOC_4096])  return kmem_cache_alloc(kmallocs[KMALLOC_4096]);

    
    return kmem_buddy_alloc(&kbuddy, size);
    
}

void *kcalloc(size_t count, size_t size) {
    size_t total_size = count * size;
    void* ptr = kmalloc(total_size);
    if (ptr) {
        
        for (size_t i = 0; i < total_size; i++) {
            ((uint8_t*)ptr)[i] = 0;
        }
    }
    return ptr;
}

void*krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    void* new_ptr = kmalloc(new_size);
    if (new_ptr) {
        for (size_t i = 0; i < new_size; i++) {
            ((uint8_t*)new_ptr)[i] = ((uint8_t*)ptr)[i];
        }
        kfree(ptr);
    }
    return new_ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;

    
    
    kmem_buddy_block_t* maybe_buddy = (kmem_buddy_block_t*)((uint8_t*)ptr - sizeof(kmem_buddy_block_t));
    if (maybe_buddy->magic == BUDDY_MAGIC) {
        kmem_buddy_free(&kbuddy, ptr);
        return;
    }

    
    
    for (size_t align = 0x1000; align <= 0x4000; align <<= 1) {
        struct slab* maybe_slab = (struct slab*)((uintptr_t)ptr & ~(align - 1));
        if (maybe_slab->magic == SLAB_MAGIC) {
            kmem_cache_free(maybe_slab->cache, ptr);
            return;
        }
    }

    LOG_ERROR("kfree received unknown pointer 0x%p!", ptr);
}

void kcfree(void* ptr, size_t count, size_t size) {
    if (ptr) {
        size_t total_size = count * size;
        memset(ptr, 0, total_size);
        kfree(ptr);
    }
}