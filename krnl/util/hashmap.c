#include "hashmap.h"
#include "mm.h"
#include "cstdlib.h"

#define HASHMAP_DEFAULT_CAPACITY 16
#define HASHMAP_LOAD_FACTOR_NUM  3
#define HASHMAP_LOAD_FACTOR_DEN  4

static kmem_cache_t* entry_cache = NULL;
static kmem_cache_t* map_cache   = NULL;

static void caches_init(void) {
    if (!entry_cache)
        entry_cache = kmem_cache_create("khashmap_entry_cache", sizeof(hashmap_entry_t));
    if (!map_cache)
        map_cache = kmem_cache_create("khashmap_cache", sizeof(hashmap_t));
}

static uint32_t hash_str(const char* key) {
    uint32_t h = 2166136261u;
    while (*key)
        h = (h ^ (uint8_t)*key++) * 16777619u;
    return h;
}

static hashmap_entry_t* entry_alloc(const char* key, void* value, uint32_t hash) {
    hashmap_entry_t* e = (hashmap_entry_t*)kmem_cache_alloc(entry_cache);
    if (!e) return NULL;
    e->key   = key;
    e->value = value;
    e->hash  = hash;
    e->next  = NULL;
    return e;
}

static void entry_free(hashmap_entry_t* e) {
    kmem_cache_free(entry_cache, e);
}

static bool rehash(hashmap_t* map, uint32_t new_cap) {
    hashmap_entry_t** new_buckets =
        (hashmap_entry_t**)khmalloc(new_cap * sizeof(hashmap_entry_t*));
    if (!new_buckets) return false;
    memset(new_buckets, 0, new_cap * sizeof(hashmap_entry_t*));

    for (uint32_t i = 0; i < map->capacity; i++) {
        hashmap_entry_t* e = map->buckets[i];
        while (e) {
            hashmap_entry_t* next = e->next;
            uint32_t idx = e->hash % new_cap;
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }
    khfree(map->buckets);
    map->buckets  = new_buckets;
    map->capacity = new_cap;
    return true;
}

hashmap_t* hashmap_create(uint32_t capacity) {
    caches_init();
    if (!entry_cache || !map_cache) return NULL;
    if (capacity == 0) capacity = HASHMAP_DEFAULT_CAPACITY;

    hashmap_t* map = (hashmap_t*)kmem_cache_alloc(map_cache);
    if (!map) return NULL;

    map->buckets = (hashmap_entry_t**)khmalloc(capacity * sizeof(hashmap_entry_t*));
    if (!map->buckets) { kmem_cache_free(map_cache, map); return NULL; }

    memset(map->buckets, 0, capacity * sizeof(hashmap_entry_t*));
    map->capacity = capacity;
    map->size     = 0;
    return map;
}

void hashmap_clear(hashmap_t* map) {
    for (uint32_t i = 0; i < map->capacity; i++) {
        hashmap_entry_t* e = map->buckets[i];
        while (e) {
            hashmap_entry_t* next = e->next;
            entry_free(e);
            e = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

void hashmap_destroy(hashmap_t* map) {
    hashmap_clear(map);
    khfree(map->buckets);
    kmem_cache_free(map_cache, map);
}

bool hashmap_set(hashmap_t* map, const char* key, void* value) {
    if (map->size >= map->capacity * HASHMAP_LOAD_FACTOR_NUM / HASHMAP_LOAD_FACTOR_DEN)
        rehash(map, map->capacity * 2);

    uint32_t h   = hash_str(key);
    uint32_t idx = h % map->capacity;

    for (hashmap_entry_t* e = map->buckets[idx]; e; e = e->next) {
        if (e->hash == h && strcmp(e->key, key) == 0) {
            e->value = value;
            return true;
        }
    }

    hashmap_entry_t* e = entry_alloc(key, value, h);
    if (!e) return false;

    e->next = map->buckets[idx];
    map->buckets[idx] = e;
    map->size++;
    return true;
}

void* hashmap_get(hashmap_t* map, const char* key) {
    uint32_t h   = hash_str(key);
    uint32_t idx = h % map->capacity;
    for (hashmap_entry_t* e = map->buckets[idx]; e; e = e->next)
        if (e->hash == h && strcmp(e->key, key) == 0)
            return e->value;
    return NULL;
}

bool hashmap_has(hashmap_t* map, const char* key) {
    uint32_t h   = hash_str(key);
    uint32_t idx = h % map->capacity;
    for (hashmap_entry_t* e = map->buckets[idx]; e; e = e->next)
        if (e->hash == h && strcmp(e->key, key) == 0)
            return true;
    return false;
}

void* hashmap_remove(hashmap_t* map, const char* key) {
    uint32_t h   = hash_str(key);
    uint32_t idx = h % map->capacity;

    hashmap_entry_t** pp = &map->buckets[idx];
    while (*pp) {
        hashmap_entry_t* e = *pp;
        if (e->hash == h && strcmp(e->key, key) == 0) {
            *pp = e->next;
            void* value = e->value;
            entry_free(e);
            map->size--;
            return value;
        }
        pp = &e->next;
    }
    return NULL;
}

uint32_t hashmap_size(const hashmap_t* map) {
    return map->size;
}

void hashmap_foreach(hashmap_t* map, hashmap_iter_fn_t fn, void* ctx) {
    for (uint32_t i = 0; i < map->capacity; i++) {
        hashmap_entry_t* e = map->buckets[i];
        while (e) {
            hashmap_entry_t* next = e->next;
            if (fn(e->key, e->value, ctx)) return;
            e = next;
        }
    }
}