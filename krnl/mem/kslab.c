#include "kslab.h"

#include "tty.h"

#include "pmm.h"
#include "kheap.h"
#include "log.h"

LOG_MODULE("slab")

#define PAGE_SIZE 4096

static kmem_cache_t* cache_chain = NULL;

extern void* phys_to_virt(uint64_t phys_addr);


static void list_remove(slab_t** list_head, slab_t* slab) {
    if (slab->prev) slab->prev->next = slab->next;
    if (slab->next) slab->next->prev = slab->prev;
    if (*list_head == slab) *list_head = slab->next;
    slab->prev = NULL;
    slab->next = NULL;
}


static void list_add(slab_t** list_head, slab_t* slab) {
    slab->next = *list_head;
    slab->prev = NULL;
    if (*list_head) (*list_head)->prev = slab;
    *list_head = slab;
}


static void count_slabs(struct slab* list, size_t* slab_count, size_t* free_objs) {
    *slab_count = 0;
    *free_objs = 0;
    struct slab* curr = list;
    while (curr) {
        (*slab_count)++;
        *free_objs += curr->free_count;
        curr = curr->next;
    }
}

void kmem_cache_dump_info(void) {
    tty_printf("=======================================================================================\n");
    tty_printf(" SLAB CACHE NAME      | OBJS (ACT / TOT)        | OBJ SIZE   | SLABS (F/P/E) \n");
    tty_printf("=======================================================================================\n");

    kmem_cache_t* curr = cache_chain;
    
    while (curr) {
        size_t full_slabs = 0, full_free = 0;
        size_t part_slabs = 0, part_free = 0;
        size_t free_slabs = 0, empty_free = 0;

        
        count_slabs(curr->slabs_full, &full_slabs, &full_free);
        count_slabs(curr->slabs_partial, &part_slabs, &part_free);
        count_slabs(curr->slabs_free, &free_slabs, &empty_free);

        
        size_t total_slabs = full_slabs + part_slabs + free_slabs;
        size_t total_objs = total_slabs * curr->objects_per_slab;
        
        
        size_t total_free_objs = part_free + empty_free; 
        size_t active_objs = total_objs - total_free_objs;

        
        
            tty_printf(" %20s | %08llu / %06llu       | %06llu B   | %06llu / %06llu / %06llu\n",
                     curr->name, 
                     active_objs, total_objs, 
                     curr->object_size,
                     full_slabs, part_slabs, free_slabs);
        

        curr = curr->next_cache;
    }
    tty_printf("=======================================================================================\n");
}


kmem_cache_t* kmem_cache_create(const char* name, size_t size) {
    
    if (size < sizeof(void*)) size = sizeof(void*);
    
    
    size = (size + 7) & ~7;

    kmem_cache_t* cache = (kmem_cache_t*)khmalloc(sizeof(kmem_cache_t));
    if (!cache) return NULL;

    cache->name = name;
    cache->object_size = size;
    
    
    size_t header_size = (sizeof(slab_t) + 7) & ~7; 
    size_t pages = 1;
    while ((pages * PAGE_SIZE - header_size) / size == 0 && pages < 16) {
        pages *= 2;
    }
    cache->pages_per_slab = pages;
    
    size_t available_space = pages * PAGE_SIZE - header_size;
    cache->objects_per_slab = available_space / size;

    if (cache->objects_per_slab == 0) {
        LOG_ERROR("Object size %d is too large even with %d pages!", size, pages);
        khfree(cache);
        return NULL;
    }

    cache->slabs_partial = NULL;
    cache->slabs_full = NULL;
    cache->slabs_free = NULL;

    cache->next_cache = cache_chain;
    cache_chain = cache;

    LOG_INFO("Created cache %s: object_size=%llu objects_per_slab=%llu pages_per_slab=%llu",
             name, cache->object_size, cache->objects_per_slab, cache->pages_per_slab);
    
    return cache;
}


static slab_t* allocate_slab_page(kmem_cache_t* cache) {
    void* phys_addr = pmm_alloc(cache->pages_per_slab);
    if (!phys_addr) return NULL;

    slab_t* slab = (slab_t*)phys_to_virt((uint64_t)phys_addr);
    
    slab->magic = SLAB_MAGIC;
    slab->cache = cache;
    slab->free_count = cache->objects_per_slab;
    slab->next = NULL;
    slab->prev = NULL;

    size_t header_size = (sizeof(slab_t) + 7) & ~7;
    uint8_t* obj_start = (uint8_t*)slab + header_size;
    
    slab->free_list = obj_start;

    
    for (size_t i = 0; i < cache->objects_per_slab - 1; i++) {
        void** current_obj = (void**)(obj_start + i * cache->object_size);
        *current_obj = (void*)(obj_start + (i + 1) * cache->object_size);
    }
    
    
    void** last_obj = (void**)(obj_start + (cache->objects_per_slab - 1) * cache->object_size);
    *last_obj = NULL;

    return slab;
}


void* kmem_cache_alloc(kmem_cache_t* cache) {
    if (!cache) return NULL;

    slab_t* slab = cache->slabs_partial;

    
    if (!slab) {
        slab = cache->slabs_free;
        if (slab) {
            list_remove(&cache->slabs_free, slab);
            list_add(&cache->slabs_partial, slab);
        }
    }

    
    if (!slab) {
        slab = allocate_slab_page(cache);
        if (!slab) return NULL;
        list_add(&cache->slabs_partial, slab);
    }

    
    void* obj = slab->free_list;
    slab->free_list = *(void**)obj; 
    slab->free_count--;

    
    if (slab->free_count == 0) {
        list_remove(&cache->slabs_partial, slab);
        list_add(&cache->slabs_full, slab);
    }

    return obj;
}


void kmem_cache_free(kmem_cache_t* cache, void* obj) {
    if (!cache || !obj) return;
    
    size_t slab_size = cache->pages_per_slab * PAGE_SIZE;
    slab_t* slab = (slab_t*)((uintptr_t)obj & ~(slab_size - 1));

    
    *(void**)obj = slab->free_list;
    slab->free_list = obj;
    slab->free_count++;

    
    if (slab->free_count == 1) {
        
        list_remove(&cache->slabs_full, slab);
        list_add(&cache->slabs_partial, slab);
    } else if (slab->free_count == cache->objects_per_slab) {
        
        list_remove(&cache->slabs_partial, slab);
        list_add(&cache->slabs_free, slab);
    }
}