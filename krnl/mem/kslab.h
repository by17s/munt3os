#pragma once

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct slab slab_t;

#define SLAB_MAGIC 0xABADC0DE


typedef struct kmem_cache {
    const char* name;
    size_t object_size;
    size_t objects_per_slab;
    size_t pages_per_slab;   
    
    slab_t* slabs_partial; 
    slab_t* slabs_full;    
    slab_t* slabs_free;    

    struct kmem_cache* next_cache;
} kmem_cache_t;


struct slab {
    void* free_list;     
    size_t free_count;   
    kmem_cache_t* cache; 
    slab_t* next;        
    slab_t* prev;        
    uint32_t magic;     
};

void kmem_cache_dump_info(void);
kmem_cache_t* kmem_cache_create(const char* name, size_t size);
void* kmem_cache_alloc(kmem_cache_t* cache);
void kmem_cache_free(kmem_cache_t* cache, void* obj);
