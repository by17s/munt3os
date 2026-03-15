#pragma once

#include <stdint.h>

#include "limine.h"

typedef struct {
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint32_t bpp; 
    uint32_t* buffer;
    uint32_t* swap;
} framebuffer_t;

int vid_fb_init(framebuffer_t* fb, struct limine_framebuffer* limine_fb);

int vid_fb_enable_swap(framebuffer_t* fb);
int vid_fb_swap(framebuffer_t* fb);

static inline int vid_fb_putpixel(framebuffer_t* fb, uint64_t x, uint64_t y, uint32_t color);
int vid_fb_clear(framebuffer_t* fb, uint32_t color);
int vid_fb_drawchar(framebuffer_t* fb, uint64_t x, uint64_t y, char c, uint32_t fg_color, uint32_t bg_color);
int vid_fb_drawchar_psf(framebuffer_t* fb, void* font_ptr, uint64_t x, uint64_t y, uint32_t c, uint32_t fg_color, uint32_t bg_color);