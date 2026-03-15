#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BUDDY_MAGIC 0x80D180D1
#define BUDDY_MIN_SHIFT 6
#define BUDDY_MAX_ORDER 18

typedef struct kmem_buddy_block {
    uint32_t magic;
    bool is_free;
    uint8_t order;
    struct kmem_buddy_block* next;
    struct kmem_buddy_block* prev;
} kmem_buddy_block_t;

typedef struct {
    void* pool_start;
    size_t pool_size;
    kmem_buddy_block_t* free_lists[BUDDY_MAX_ORDER + 1];
} kmem_buddy_allocator_t;


void kmem_buddy_init(kmem_buddy_allocator_t* alloc, void* pool_start, size_t pool_size);
void* kmem_buddy_alloc(kmem_buddy_allocator_t* alloc, size_t size);
void kmem_buddy_free(kmem_buddy_allocator_t* alloc, void* ptr);