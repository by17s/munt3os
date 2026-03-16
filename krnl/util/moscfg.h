#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CFG_INT,
    CFG_STR,
    CFG_ARRAY,
} cfg_value_type_t;

typedef struct {
    int32_t* items;
    uint32_t count;
} cfg_array_t;

typedef struct {
    cfg_value_type_t type;
    union {
        int32_t    i;
        char*      s;
        cfg_array_t arr;
    };
} cfg_value_t;

typedef struct cfg_entry {
    char*             name;
    cfg_value_t       value;
    struct cfg_entry* next;
} cfg_entry_t;

typedef struct {
    cfg_entry_t* entries;
    uint32_t     count;
} cfg_t;

cfg_t* cfg_parse_file(const char* path);
cfg_t* cfg_parse_buf(const char* buf, size_t len);
void cfg_destroy(cfg_t* cfg);

cfg_entry_t* cfg_get(const cfg_t* cfg, const char* name);
int32_t      cfg_get_int(const cfg_t* cfg, const char* name, int32_t fallback);
const char*  cfg_get_str(const cfg_t* cfg, const char* name, const char* fallback);
cfg_array_t* cfg_get_array(const cfg_t* cfg, const char* name);