#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "hw/video.h"
#include "util/ring_buffer.h"
#include "fs/vfs.h"

#include "color.h"


typedef struct tty_s {
    uint64_t flags;
    uint64_t cols;
    uint64_t rows;
    uint64_t cursor_col;
    uint64_t cursor_row;
    uint32_t fg_color;
    uint32_t bg_color;

    char* pwd;
    vfs_node_t* cwd;

    void* font;
    framebuffer_t* fb;
    uint64_t fb_x;
    uint64_t fb_y;

    
    int (*putchar)(struct tty_s* tty, char c);
    int (*puts)(struct tty_s* tty, const char* str);
    int (*printf)(struct tty_s* tty, const char* format, ...);
    int (*vprintf)(struct tty_s* tty, const char* format, va_list args);
    int (*push_char)(struct tty_s* tty, char c);
    int (*getchar)(struct tty_s* tty);
    int (*clear)(struct tty_s* tty);
    int (*scroll)(struct tty_s* tty, int lines);
    int (*flash)(struct tty_s* tty);

    int (*cd)(struct tty_s* tty, const char* path);
    int (*_cd_node)(struct tty_s* tty, vfs_node_t* node);

    
    bool freezed;

    
    ring_buffer_t input_buffer;

    
    int ansi_state;       
    char ansi_buf[32];    
    int ansi_buf_idx;     
    pallet_t ansi_pallet;

    struct {
        uint64_t fg_color_index;
        uint64_t bg_color_index;
    } colors;
} tty_t;

int tty_get(int64_t index, tty_t** out);
int tty_set_active(int64_t index);
int tty_printf(const char* __fmt, ...);
int tty_init(tty_t* tty, framebuffer_t *fb);

int tty_load_cfg(tty_t *tty, const char* path);