#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


typedef struct {
    uint64_t total_size;
    uint64_t used_size;
    uint64_t free_size;
    uint64_t block_count;
    bool initialized;
} kheap_stat_t;


void kheap_init(size_t initial_pages);
void* khmalloc(size_t size);
void* khcalloc(size_t num, size_t size);
void* khrealloc(void* ptr, size_t new_size);
void khfree(void* ptr);
void* khaligned_alloc(size_t alignment, size_t size);
bool kheap_is_free(void* ptr);
kheap_stat_t kheap_get_stat(void);