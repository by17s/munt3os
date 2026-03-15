#include "psf.h"
#include <stddef.h>

int psf_init_font(psf_font_t* font, void* memory) {
    if (!font || !memory) return -1;
    
    psf1_header_t* p1 = (psf1_header_t*)memory;
    psf2_header_t* p2 = (psf2_header_t*)memory;
    
    if (p1->magic[0] == PSF1_MAGIC0 && p1->magic[1] == PSF1_MAGIC1) {
        font->version = 1;
        font->width = 8;
        font->height = p1->charsize;
        font->bytesperglyph = p1->charsize;
        font->numglyph = (p1->mode & PSF1_MODE512) ? 512 : 256;
        font->glyphs = (void*)((uintptr_t)memory + sizeof(psf1_header_t));
        return 0;
    } else if (p2->magic[0] == PSF2_MAGIC0 && p2->magic[1] == PSF2_MAGIC1 &&
               p2->magic[2] == PSF2_MAGIC2 && p2->magic[3] == PSF2_MAGIC3) {
        font->version = 2;
        font->width = p2->width;
        font->height = p2->height;
        font->bytesperglyph = p2->bytesperglyph;
        font->numglyph = p2->numglyph;
        font->glyphs = (void*)((uintptr_t)memory + p2->headersize);
        return 0;
    }
    
    return -1;
}
