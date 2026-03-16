#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct list_node {
    struct list_node* prev;
    struct list_node* next;
    void*             value;
} list_node_t;

typedef struct {
    list_node_t* head;
    list_node_t* tail;
    uint32_t     size;
} list_t;

typedef bool (*list_iter_fn_t)(list_node_t* node, void* ctx);
typedef bool (*list_pred_fn_t)(void* value, void* ctx);

list_t* list_create(void);
void list_destroy(list_t* list);
void list_clear(list_t* list);

// push
list_node_t* list_push_front    (list_t* list, void* value);
list_node_t* list_push_back     (list_t* list, void* value);
list_node_t* list_insert_before (list_t* list, list_node_t* anchor, void* value);
list_node_t* list_insert_after  (list_t* list, list_node_t* anchor, void* value);

// pop
void* list_pop_front(list_t* list);
void* list_pop_back(list_t* list);
// rm rf /
void         list_remove     (list_t* list, list_node_t* node);
void*        list_remove_if  (list_t* list, list_pred_fn_t pred, void* ctx);
uint32_t     list_remove_all (list_t* list, list_pred_fn_t pred, void* ctx);

// access / search
list_node_t* list_front (const list_t* list);
list_node_t* list_back  (const list_t* list);
list_node_t* list_at    (const list_t* list, uint32_t index);
list_node_t* list_find  (const list_t* list, list_pred_fn_t pred, void* ctx);

// queries
bool list_empty     (const list_t* list);
uint32_t list_size  (const list_t* list);

// callback traversal
void list_foreach_cb     (list_t* list, list_iter_fn_t fn, void* ctx);
void list_foreach_rev_cb (list_t* list, list_iter_fn_t fn, void* ctx);

// bulk
void list_splice_back (list_t* dst, list_t* src);
void list_reverse     (list_t* list);


#define list_foreach(list, cursor) \
    for (list_node_t* cursor = (list)->head; \
         cursor != NULL; \
         cursor = cursor->next)

#define list_foreach_reverse(list, cursor) \
    for (list_node_t* cursor = (list)->tail; \
         cursor != NULL; \
         cursor = cursor->prev)

#define list_foreach_safe(list, cursor, tmp) \
    for (list_node_t* cursor = (list)->head, \
                    * tmp    = (cursor ? cursor->next : NULL); \
         cursor != NULL; \
         cursor = tmp, tmp = (tmp ? tmp->next : NULL))

#define list_foreach_safe_reverse(list, cursor, tmp) \
    for (list_node_t* cursor = (list)->tail, \
                    * tmp    = (cursor ? cursor->prev : NULL); \
         cursor != NULL; \
         cursor = tmp, tmp = (tmp ? tmp->prev : NULL))

         