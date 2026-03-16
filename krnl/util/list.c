#include "list.h"
#include "mm.h"

static kmem_cache_t *list_mem_node_cache = NULL;
static kmem_cache_t *list_mem_cache = NULL;

static list_node_t* node_alloc(void* value) {
    list_node_t* n = (list_node_t*)kmem_cache_alloc(list_mem_node_cache);
    if (!n) return NULL;
    n->prev  = NULL;
    n->next  = NULL;
    n->value = value;
    return n;
}

static void node_free(list_node_t* n) {
    kmem_cache_free(list_mem_node_cache, n);
}


static void node_link(list_t* list, list_node_t* prev,
                      list_node_t* node, list_node_t* next) {
    node->prev = prev;
    node->next = next;

    if (prev) prev->next = node; else list->head = node;
    if (next) next->prev = node; else list->tail = node;

    list->size++;
}


static void node_unlink(list_t* list, list_node_t* node) {
    if (node->prev) node->prev->next = node->next; else list->head = node->next;
    if (node->next) node->next->prev = node->prev; else list->tail = node->prev;
    node->prev = NULL;
    node->next = NULL;
    list->size--;
}

list_t* list_create(void) {
    if(!list_mem_node_cache) {
        list_mem_node_cache = kmem_cache_create("list_node_cache", sizeof(list_node_t));
        if (!list_mem_node_cache) return NULL;
    }
    if(!list_mem_cache) {
        list_mem_cache = kmem_cache_create("list_cache", sizeof(list_t));
        if (!list_mem_cache) return NULL;
    }
    
    list_t* list = (list_t*)kmem_cache_alloc(list_mem_cache);
    if (!list) 
        return NULL;

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    return list;
}

void list_clear(list_t* list) {
    list_node_t* cur = list->head;
    while (cur) {
        list_node_t* next = cur->next;
        node_free(cur);
        cur = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void list_destroy(list_t* list) {
    list_clear(list);
    kmem_cache_free(list_mem_cache, list);
}

list_node_t* list_push_front(list_t* list, void* value) {
    list_node_t* n = node_alloc(value);
    if (!n) return NULL;
    node_link(list, NULL, n, list->head);
    return n;
}

list_node_t* list_push_back(list_t* list, void* value) {
    list_node_t* n = node_alloc(value);
    if (!n) return NULL;
    node_link(list, list->tail, n, NULL);
    return n;
}

list_node_t* list_insert_before(list_t* list, list_node_t* anchor, void* value) {
    if (!anchor) return list_push_back(list, value);
    list_node_t* n = node_alloc(value);
    if (!n) return NULL;
    node_link(list, anchor->prev, n, anchor);
    return n;
}

list_node_t* list_insert_after(list_t* list, list_node_t* anchor, void* value) {
    if (!anchor) return list_push_front(list, value);
    list_node_t* n = node_alloc(value);
    if (!n) return NULL;
    node_link(list, anchor, n, anchor->next);
    return n;
}

void* list_pop_front(list_t* list) {
    if (!list->head) return NULL;
    list_node_t* n = list->head;
    void* value = n->value;
    node_unlink(list, n);
    node_free(n);
    return value;
}

void* list_pop_back(list_t* list) {
    if (!list->tail) return NULL;
    list_node_t* n = list->tail;
    void* value = n->value;
    node_unlink(list, n);
    node_free(n);
    return value;
}

void list_remove(list_t* list, list_node_t* node) {
    node_unlink(list, node);
    node_free(node);
}

void* list_remove_if(list_t* list, list_pred_fn_t pred, void* ctx) {
    list_node_t* cur = list->head;
    while (cur) {
        list_node_t* next = cur->next;
        if (pred(cur->value, ctx)) {
            void* value = cur->value;
            node_unlink(list, cur);
            node_free(cur);
            return value;
        }
        cur = next;
    }
    return NULL;
}

uint32_t list_remove_all(list_t* list, list_pred_fn_t pred, void* ctx) {
    uint32_t count = 0;
    list_node_t* cur = list->head;
    while (cur) {
        list_node_t* next = cur->next;
        if (pred(cur->value, ctx)) {
            node_unlink(list, cur);
            node_free(cur);
            count++;
        }
        cur = next;
    }
    return count;
}

list_node_t* list_front(const list_t* list) { return list->head; }
list_node_t* list_back (const list_t* list) { return list->tail; }

list_node_t* list_at(const list_t* list, uint32_t index) {
    if (index >= list->size) return NULL;
    if (index < list->size / 2) {
        list_node_t* cur = list->head;
        for (uint32_t i = 0; i < index; i++) cur = cur->next;
        return cur;
    } else {
        list_node_t* cur = list->tail;
        for (uint32_t i = list->size - 1; i > index; i--) cur = cur->prev;
        return cur;
    }
}

list_node_t* list_find(const list_t* list, list_pred_fn_t pred, void* ctx) {
    list_node_t* cur = list->head;
    while (cur) {
        if (pred(cur->value, ctx)) return cur;
        cur = cur->next;
    }
    return NULL;
}

bool     list_empty(const list_t* list) { return list->size == 0; }
uint32_t list_size (const list_t* list) { return list->size; }

void list_foreach_cb(list_t* list, list_iter_fn_t fn, void* ctx) {
    list_node_t* cur = list->head;
    while (cur) {
        list_node_t* next = cur->next; 
        if (fn(cur, ctx)) break;
        cur = next;
    }
}

void list_foreach_rev_cb(list_t* list, list_iter_fn_t fn, void* ctx) {
    list_node_t* cur = list->tail;
    while (cur) {
        list_node_t* prev = cur->prev;
        if (fn(cur, ctx)) break;
        cur = prev;
    }
}

void list_splice_back(list_t* dst, list_t* src) {
    if (!src->head) return;

    if (!dst->tail) {
        dst->head = src->head;
        dst->tail = src->tail;
    } else {
        dst->tail->next  = src->head;
        src->head->prev  = dst->tail;
        dst->tail        = src->tail;
    }

    dst->size += src->size;
    src->head = NULL;
    src->tail = NULL;
    src->size = 0;
}

void list_reverse(list_t* list) {
    list_node_t* cur = list->head;
    while (cur) {
        list_node_t* tmp = cur->next;
        cur->next = cur->prev;
        cur->prev = tmp;
        cur = tmp;
    }
    list_node_t* tmp = list->head;
    list->head = list->tail;
    list->tail = tmp;
}