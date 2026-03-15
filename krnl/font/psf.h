#pragma once

#include <stdint.h>

#define PSF1_MAGIC0     0x36
#define PSF1_MAGIC1     0x04

#define PSF1_MODE512    0x01
#define PSF1_MODEHASTAB 0x02
#define PSF1_MODEHASSEQ 0x04
#define PSF1_MAXMODE    0x05

#define PSF1_SEPARATOR  0xFFFF
#define PSF1_STARTSEQ   0xFFFE

typedef struct {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize;
} __attribute__((packed)) psf1_header_t;

typedef struct {
    psf1_header_t* header;
    void* glyphs;
} psf1_font_t;


#define PSF2_MAGIC0     0x72
#define PSF2_MAGIC1     0xb5
#define PSF2_MAGIC2     0x4a
#define PSF2_MAGIC3     0x86


#define PSF2_HAS_UNICODE_TABLE 0x01

#define PSF2_MAXVERSION 0

#define PSF2_SEPARATOR  0xFF
#define PSF2_STARTSEQ   0xFE

typedef struct {
    uint8_t magic[4];
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t numglyph;
    uint32_t bytesperglyph;
    uint32_t height;
    uint32_t width;
} __attribute__((packed)) psf2_header_t;

typedef struct {
    psf2_header_t* header;
    void* glyphs;
} psf2_font_t;

typedef struct {
    int version; 
    uint32_t width;
    uint32_t height;
    uint32_t bytesperglyph;
    uint32_t numglyph;
    void* glyphs;
} psf_font_t;

int psf_init_font(psf_font_t* font, void* memory);
