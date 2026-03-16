#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct hashmap_entry {
    const char*          key;
    void*                value;
    uint32_t             hash;
    struct hashmap_entry* next;
} hashmap_entry_t;

typedef struct {
    hashmap_entry_t** buckets;
    uint32_t          capacity;
    uint32_t          size;
} hashmap_t;

typedef bool (*hashmap_iter_fn_t)(const char* key, void* value, void* ctx);

hashmap_t* hashmap_create(uint32_t capacity);
void hashmap_destroy(hashmap_t* map);
void hashmap_clear(hashmap_t* map);

bool  hashmap_set(hashmap_t* map, const char* key, void* value);
void* hashmap_get(hashmap_t* map, const char* key);
bool  hashmap_has(hashmap_t* map, const char* key);
void* hashmap_remove(hashmap_t* map, const char* key);

uint32_t hashmap_size(const hashmap_t* map);

void hashmap_foreach(hashmap_t* map, hashmap_iter_fn_t fn, void* ctx);

#define HASHMAP_FOREACH(map, k, v) \
    for (uint32_t _bi = 0; _bi < (map)->capacity; _bi++) \
        for (hashmap_entry_t* _e = (map)->buckets[_bi]; _e; _e = _e->next) \
            for (const char* k = _e->key; k; k = NULL) \
                for (void* v = _e->value; k; k = NULL, v = NULL)
                