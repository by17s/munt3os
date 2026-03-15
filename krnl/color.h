#pragma once

#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))

typedef int color_t;

enum {
    ANSI_NUM_BLACK = 0,
    ANSI_NUM_BLACK_BOLD = 1,
    ANSI_NUM_RED = 2,
    ANSI_NUM_RED_BOLD = 3,
    ANSI_NUM_GREEN = 4,
    ANSI_NUM_GREEN_BOLD = 5,
    ANSI_NUM_YELLOW = 6,
    ANSI_NUM_YELLOW_BOLD = 7,
    ANSI_NUM_BLUE = 8,
    ANSI_NUM_BLUE_BOLD = 9,
    ANSI_NUM_MAGENTA = 10,
    ANSI_NUM_MAGENTA_BOLD = 11,
    ANSI_NUM_CYAN = 12,
    ANSI_NUM_CYAN_BOLD = 13,
    ANSI_NUM_WHITE = 14,
    ANSI_NUM_WHITE_BOLD = 15
};

typedef struct pallet_s {
    color_t colors[16];
} pallet_t;