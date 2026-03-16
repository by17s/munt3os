#include "moscfg.h"
#include "mm.h"
#include "cstdlib.h"
#include "log.h"
#include "fs/vfs.h"

LOG_MODULE("moscfg")

static char* kstrdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* p = (char*)kmalloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

static char* kstrndup(const char* s, size_t n) {
    char* p = (char*)kmalloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    return p;
}

static const char* skip_to_eol(const char* p) {
    while (*p && *p != '\n') p++;
    return p;
}

static bool parse_int(const char* s, const char* end, int32_t* out) {
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (end > s && (*(end-1) == ' ' || *(end-1) == '\t')) end--;
    if (s >= end) return false;

    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }
    if (s >= end) return false;

    int32_t val = 0;
    if (s + 1 < end && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        if (s >= end) return false;
        while (s < end) {
            char c = *s++;
            if      (c >= '0' && c <= '9') val = val * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') val = val * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = val * 16 + (c - 'A' + 10);
            else return false;
        }
    } else if (*s == '0' && end - s > 1) {
        s++;
        while (s < end) {
            char c = *s++;
            if (c < '0' || c > '7') return false;
            val = val * 8 + (c - '0');
        }
    } else {
        while (s < end) {
            char c = *s++;
            if (c < '0' || c > '9') return false;
            val = val * 10 + (c - '0');
        }
    }
    *out = neg ? -val : val;
    return true;
}

static bool parse_array(const char* s, const char* end, cfg_array_t* out) {
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (end > s && (*(end-1) == ' ' || *(end-1) == '\t')) end--;
    if (s >= end || *s != '[') return false;
    s++;
    if (end > s && *(end-1) == ']') end--;

    // Count commas to pre-allocate
    uint32_t cap = 1;
    for (const char* p = s; p < end; p++)
        if (*p == ',') cap++;

    int32_t* items = (int32_t*)kmalloc(cap * sizeof(int32_t));
    if (!items) return false;

    uint32_t count = 0;
    while (s < end) {
        const char* comma = s;
        while (comma < end && *comma != ',') comma++;

        int32_t v;
        if (!parse_int(s, comma, &v)) {
            kfree(items);
            return false;
        }
        items[count++] = v;
        s = (comma < end) ? comma + 1 : end;
    }

    out->items = items;
    out->count = count;
    return true;
}

static char* parse_str_value(const char* s, const char* end) {
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    while (end > s && (*(end-1) == ' ' || *(end-1) == '\t')) end--;
    // Strip optional surrounding quotes
    if (end - s >= 2 && *s == '"' && *(end-1) == '"') { s++; end--; }
    return kstrndup(s, (size_t)(end - s));
}

static cfg_entry_t* entry_alloc(void) {
    cfg_entry_t* e = (cfg_entry_t*)kmalloc(sizeof(cfg_entry_t));
    if (!e) return NULL;
    memset(e, 0, sizeof(*e));
    return e;
}

static void entry_free(cfg_entry_t* e) {
    if (!e) return;
    kfree(e->name);
    if (e->value.type == CFG_STR)   kfree(e->value.s);
    if (e->value.type == CFG_ARRAY) kfree(e->value.arr.items);
    kfree(e);
}

static cfg_entry_t* parse_line(const char* line, const char* end) {
    line = skip_ws(line);
    if (line >= end) return NULL;

    // NAME
    const char* name_start = line;
    while (line < end && *line != ':' && *line != ' ' && *line != '\t') line++;
    if (line >= end || name_start == line) return NULL;
    const char* name_end = line;

    // ':'
    line = skip_ws(line);
    if (line >= end || *line != ':') return NULL;
    line++;

    // TYPE keyword
    line = skip_ws(line);
    const char* type_start = line;
    while (line < end && *line != '=' && *line != ' ' && *line != '\t') line++;
    const char* type_end = line;
    size_t type_len = (size_t)(type_end - type_start);

    // '='
    line = skip_ws(line);
    if (line >= end || *line != '=') return NULL;
    line++;                          // skip '='
    // do NOT skip_ws here — str values may need the raw remainder

    cfg_entry_t* e = entry_alloc();
    if (!e) return NULL;

    e->name = kstrndup(name_start, (size_t)(name_end - name_start));
    if (!e->name) { kfree(e); return NULL; }

    if (type_len == 3 && memcmp(type_start, "int", 3) == 0) {
        e->value.type = CFG_INT;
        if (!parse_int(line, end, &e->value.i)) {
            LOG_WARN("moscfg: bad int for '%s'", e->name);
            entry_free(e); return NULL;
        }
    } else if (type_len == 3 && memcmp(type_start, "str", 3) == 0) {
        e->value.type = CFG_STR;
        e->value.s = parse_str_value(line, end);
        if (!e->value.s) { entry_free(e); return NULL; }
    } else if (type_len == 5 && memcmp(type_start, "array", 5) == 0) {
        e->value.type = CFG_ARRAY;
        if (!parse_array(line, end, &e->value.arr)) {
            LOG_WARN("moscfg: bad array for '%s'", e->name);
            entry_free(e); return NULL;
        }
    } else {
        LOG_WARN("moscfg: unknown type '%.*s'", (int)type_len, type_start);
        entry_free(e); return NULL;
    }

    return e;
}

cfg_t* cfg_parse_buf(const char* buf, size_t len) {
    cfg_t* cfg = (cfg_t*)kmalloc(sizeof(cfg_t));
    if (!cfg) return NULL;
    cfg->entries = NULL;
    cfg->count   = 0;

    cfg_entry_t* tail = NULL;
    const char* p   = buf;
    const char* eof = buf + len;

    while (p < eof) {
        // skip blank lines and comments
        p = skip_ws(p);
        if (p >= eof) break;
        if (*p == '\n') { p++; continue; }
        if (*p == '#' || *p == ';') { p = skip_to_eol(p); continue; }

        const char* line_end = p;
        while (line_end < eof && *line_end != '\n') line_end++;

        // Strip inline comments
        const char* eol = p;
        while (eol < line_end && *eol != '#' && *eol != ';') eol++;

        cfg_entry_t* e = parse_line(p, eol);
        if (e) {
            e->next = NULL;
            if (!tail) { cfg->entries = e; tail = e; }
            else       { tail->next = e;   tail = e; }
            cfg->count++;
        }

        p = (line_end < eof) ? line_end + 1 : eof;
    }
    return cfg;
}

cfg_t* cfg_parse_file(const char* path) {
    vfs_node_t* node = kopen(path);
    if (!node) {
        LOG_ERROR("moscfg: cannot open '%s'", path);
        return NULL;
    }

    uint32_t size = node->size;
    char* buf = (char*)kmalloc(size + 1);
    if (!buf) { kclose(node); return NULL; }

    uint32_t got = vfs_read(node, 0, size, (uint8_t*)buf);
    kclose(node);
    buf[got] = '\0';

    cfg_t* cfg = cfg_parse_buf(buf, got);
    kfree(buf);
    return cfg;
}


void cfg_destroy(cfg_t* cfg) {
    if (!cfg) return;
    cfg_entry_t* e = cfg->entries;
    while (e) {
        cfg_entry_t* next = e->next;
        entry_free(e);
        e = next;
    }
    kfree(cfg);
}

cfg_entry_t* cfg_get(const cfg_t* cfg, const char* name) {
    for (cfg_entry_t* e = cfg->entries; e; e = e->next)
        if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

int32_t cfg_get_int(const cfg_t* cfg, const char* name, int32_t fallback) {
    cfg_entry_t* e = cfg_get(cfg, name);
    if (!e || e->value.type != CFG_INT) return fallback;
    return e->value.i;
}

const char* cfg_get_str(const cfg_t* cfg, const char* name, const char* fallback) {
    cfg_entry_t* e = cfg_get(cfg, name);
    if (!e || e->value.type != CFG_STR) return fallback;
    return e->value.s;
}

cfg_array_t* cfg_get_array(const cfg_t* cfg, const char* name) {
    cfg_entry_t* e = cfg_get(cfg, name);
    if (!e || e->value.type != CFG_ARRAY) return NULL;
    return &e->value.arr;
}