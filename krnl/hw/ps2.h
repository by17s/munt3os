#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t flags;
    int8_t  x_offset;
    int8_t  y_offset;
} ps2_mouse_packet_t;

void ps2_init(void);