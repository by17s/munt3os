#include "video.h"

#include "limine.h"
#include "cstdlib.h"

#include "font6x8.h"
#include "font/psf.h"

#include "mm.h"

extern const unsigned char font_6x8_slim[FONT_6X8_SLIM_ARRAY_LENGTH];

int vid_fb_init(framebuffer_t* fb, struct limine_framebuffer* limine_fb) {
    if (!fb) return -1;
    
    fb->buffer  = (uint32_t*)limine_fb->address;
    fb->swap    = (void*)0; 

    fb->width   = limine_fb->width;
    fb->height  = limine_fb->height;
    fb->pitch   = limine_fb->pitch;
    fb->bpp     = limine_fb->bpp;

    return 0;
}

int vid_fb_enable_swap(framebuffer_t* fb) {
    if (!fb) return -1;
    if (fb->swap) return 0; 

    if (!fb || !fb->buffer) return -1;
    fb->swap = fb->buffer;

    size_t buffer_size = fb->height * fb->width * (fb->bpp / 8);

    fb->buffer = kmalloc(buffer_size);
    if (!fb->buffer) return -1;
    vid_fb_clear(fb, 0x00000000);
    return 0;
}
    

int vid_fb_swap(framebuffer_t* fb) {
    if (!fb || !fb->buffer || !fb->swap) return -1;
    memcpy_fast(fb->swap, fb->buffer, fb->height * fb->width * (fb->bpp / 8));
    return 0;
}

int vid_fb_clear(framebuffer_t* fb, uint32_t color) {
    if (!fb || !fb->buffer) return -1;

    for (uint64_t y = 0; y < fb->height; y++) {
        for (uint64_t x = 0; x < fb->width; x++) {
            fb->buffer[y * (fb->pitch / 4) + x] = color;
        }
    }

    return 0;
}

static inline int vid_fb_putpixel(framebuffer_t* fb, uint64_t x, uint64_t y, uint32_t color) {
    if (!fb || !fb->buffer) return -1;
    if (x >= fb->width || y >= fb->height) return -1;

    fb->buffer[y * (fb->pitch / 4) + x] = color;
    return 0;
}

int vid_fb_drawchar(framebuffer_t* fb, uint64_t x, uint64_t y, char c, uint32_t fg_color, uint32_t bg_color) {
    if (!fb || !fb->buffer) return -1;
    if (c < FONT_6X8_SLIM_START_CHAR || c >= FONT_6X8_SLIM_START_CHAR + FONT_6X8_SLIM_LENGTH) return -1;

    const unsigned char* glyph = &font_6x8_slim[(c - FONT_6X8_SLIM_START_CHAR) * FONT_6X8_SLIM_CHAR_HEIGHT];

    for (uint64_t row = 0; row < FONT_6X8_SLIM_CHAR_HEIGHT; row++) {
        for (uint64_t col = 0; col < FONT_6X8_SLIM_CHAR_WIDTH; col++) {
            uint32_t color = (glyph[row] & (1 <<  col)) ? fg_color : bg_color;
            vid_fb_putpixel(fb, x + col, y + row, color);
        }
    }

    return 0;
}

int vid_fb_drawchar_psf(framebuffer_t* fb, void* font_ptr, uint64_t x, uint64_t y, uint32_t c, uint32_t fg_color, uint32_t bg_color) {
    if(!fb || !fb->buffer || !font_ptr) return -1;
    psf_font_t* font = (psf_font_t*)font_ptr;

    uint32_t index = (uint32_t)(uint8_t)c;
    if (index >= font->numglyph) index = '?';

    uint8_t* glyph = (uint8_t*)font->glyphs + (index * font->bytesperglyph);

    uint32_t bytes_per_line = (font->width + 7) / 8;
    for (uint32_t row = 0; row < font->height; row++) {
        for (uint32_t col = 0; col < font->width; col++) {
            uint32_t byte_idx = col / 8;
            uint32_t bit_idx  = 7 - (col % 8);
            
            uint32_t color = (glyph[row * bytes_per_line + byte_idx] & (1 << bit_idx)) ? fg_color : bg_color;
            vid_fb_putpixel(fb, x + col, y + row, color);
        }
    }
    return 0;
}



