#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <cstdlib.h>

typedef struct {
    uint8_t* buffer;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} ring_buffer_t;

int ring_buffer_init(ring_buffer_t* rb, size_t size);
size_t ring_buffer_write(ring_buffer_t* rb, const uint8_t* data, size_t len);
size_t ring_buffer_read(ring_buffer_t* rb, uint8_t* data, size_t len);
size_t ring_buffer_available(ring_buffer_t* rb);
size_t ring_buffer_free_space(ring_buffer_t* rb);
void ring_buffer_clear(ring_buffer_t* rb);

