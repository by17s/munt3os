
#include "ring_buffer.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <mem/pmm.h>

extern void* phys_to_virt(uint64_t phys_addr);


int ring_buffer_init(ring_buffer_t* rb, size_t size) {
    if(size == 0) 
        return -1;
    if (!rb) 
        return -1;
        
    size_t alloc_size = (size + 0xFFF) & ~0xFFF;
    rb->buffer = (void*)phys_to_virt((uint64_t)pmm_alloc(alloc_size / 0x1000));
    if (!rb->buffer) 
        return -2;
    
    rb->capacity = alloc_size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return 0;
}


size_t ring_buffer_write(ring_buffer_t* rb, const uint8_t* data, size_t len) {
    if (!rb || !data || len == 0) return 0;

    if (len > rb->capacity) {
        data += len - rb->capacity;
        len = rb->capacity;
    }

    for (size_t i = 0; i < len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->capacity;
        
        if (rb->count < rb->capacity) {
            rb->count++;
        } else {
            rb->tail = (rb->tail + 1) % rb->capacity;
        }
    }
    return len;
}


size_t ring_buffer_read(ring_buffer_t* rb, uint8_t* data, size_t len) {
    if (!rb || !data || len == 0 || rb->count == 0) return 0;

    size_t read_count = 0;
    while (read_count < len && rb->count > 0) {
        data[read_count] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        read_count++;
    }
    return read_count;
}

size_t ring_buffer_available(ring_buffer_t* rb) {
    if (!rb) return 0;
    return rb->count;
}

size_t ring_buffer_free_space(ring_buffer_t* rb) {
    if (!rb) return 0;
    return rb->capacity - rb->count;
}

void ring_buffer_clear(ring_buffer_t* rb) {
    if (!rb) return;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}
