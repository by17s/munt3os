#include "tty.h"

#include "cstdlib.h"
#include "mem/kheap.h"
#include "font/psf.h"
#include "mm.h"

#include "util/moscfg.h"

#include "log.h"
LOG_MODULE("tty");

static tty_t tty_array[4];
static tty_t* active_tty = NULL;
static cfg_t* tty_cfg = NULL;
static pallet_t default_ansi_pallet = {
    .colors = {
        0x000000, 0x555555, 0xAA0000, 0xFF5555,
        0x00AA00, 0x55FF55, 0xAAAA00, 0xFFFF55,
        0x0000AA, 0x5555FF, 0xAA00AA, 0xFF55FF,
        0x00AAAA, 0x55FFFF, 0xAAAAAA, 0xFFFFFF
    }
};

int tty_get(int64_t index, tty_t** out) {
    if(index == -1) {
        *out = active_tty;
        return 0;
    }
    if (index >= 4 || !out || index < 0) return -1;
    *out = &tty_array[index];
    return 0;
}

int tty_set_active(int64_t index) {
    if (index >= 4 || index < 0) return -1;
    active_tty = &tty_array[index];
    return 0;
}

static void __tty_handle_ansi_c(tty_t* tty, char c) {
    if (tty->ansi_state == 1) {
        if (c == '[') {
            tty->ansi_state = 2; 
            tty->ansi_buf_idx = 0;
        } else {
            tty->ansi_state = 0;
            
        }
        return;
    }
    
    if (tty->ansi_state == 2) {
        if ((c >= '0' && c <= '9') || c == ';') {
            if (tty->ansi_buf_idx < sizeof(tty->ansi_buf) - 1) {
                tty->ansi_buf[tty->ansi_buf_idx++] = c;
            }
            return;
        }
        
        
        tty->ansi_buf[tty->ansi_buf_idx] = '\0';
        tty->ansi_state = 0;

        uint32_t char_w = tty->font ? ((psf_font_t*)tty->font)->width : 6;
        uint32_t char_h = tty->font ? ((psf_font_t*)tty->font)->height : 8;

        if (c == 'm') {
            
            int args[16] = {0};
            int nargs = 0;
            char* p = tty->ansi_buf;
            while (*p) {
                int val = 0;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                }
                if (nargs < 16) {
                    args[nargs++] = val;
                }
                if (*p == ';') p++;
            }
            if (nargs == 0) { nargs = 1; args[0] = 0; } 

            for (int i = 0; i < nargs; i++) {
                int val = args[i];
                if (val == 0) {
                    tty->fg_color = tty->ansi_pallet.colors[tty->colors.fg_color_index];
                    tty->bg_color = tty->ansi_pallet.colors[tty->colors.bg_color_index];
                } else if (val >= 30 && val <= 37) {
                    tty->fg_color = tty->ansi_pallet.colors[(val - 30) * 2];
                } else if (val >= 40 && val <= 47) {
                    tty->bg_color = tty->ansi_pallet.colors[(val - 40) * 2];
                } else if (val >= 90 && val <= 97) {
                    tty->fg_color = tty->ansi_pallet.colors[(val - 90) * 2 + 1];
                } else if (val >= 100 && val <= 107) {
                    tty->bg_color = tty->ansi_pallet.colors[(val - 100) * 2 + 1];
                }
            }
        } else if (c == 'J') {
            
            int val = 0;
            if (tty->ansi_buf_idx > 0) val = tty->ansi_buf[0] - '0';
            if (val == 2 && tty->clear) tty->clear(tty);
        } else if (c == 'H' || c == 'f') {
            
            int arg1 = 1, arg2 = 1;
            char* p = tty->ansi_buf;
            if (*p) {
                arg1 = 0;
                while (*p >= '0' && *p <= '9') { arg1 = arg1 * 10 + (*p - '0'); p++; }
                if (*p == ';') {
                    p++;
                    arg2 = 0;
                    while (*p >= '0' && *p <= '9') { arg2 = arg2 * 10 + (*p - '0'); p++; }
                }
            }
            if (arg1 < 1) arg1 = 1;
            if (arg2 < 1) arg2 = 1;
            tty->fb_y = (arg1 - 1) * char_h;
            tty->fb_x = (arg2 - 1) * char_w;
        }
    }
}

static int __tty_putchar_impl(tty_t* tty, char c) {
    if (c == '\033') {
        tty->ansi_state = 1;
        return 0;
    }
    if (tty->ansi_state) {
        __tty_handle_ansi_c(tty, c);
        return 0;
    }

    uint32_t char_w = 6;

    uint32_t char_h = 8;
    if(tty->font) {
        char_w = ((psf_font_t*)tty->font)->width;
        char_h = ((psf_font_t*)tty->font)->height;
    }

    if(c == '\n') {
        tty->fb_x = 0;
        tty->fb_y += char_h;
        
    } else if(c == '\b') {
        if (tty->fb_x >= char_w) {
            tty->fb_x -= char_w;
        } else if (tty->fb_y >= char_h) {
            tty->fb_y -= char_h;
            tty->fb_x = (tty->fb->width / char_w) * char_w - char_w;
        } else {
            tty->fb_x = 0;
        }
        
        if(tty->font) {
            vid_fb_drawchar_psf(tty->fb, tty->font, tty->fb_x, tty->fb_y, ' ', tty->fg_color, tty->bg_color);
        } else {
            vid_fb_drawchar(tty->fb, tty->fb_x, tty->fb_y, ' ', tty->fg_color, tty->bg_color);
        }
    } else if (tty->fb) {
        if(tty->font) {
            vid_fb_drawchar_psf(tty->fb, tty->font, tty->fb_x, tty->fb_y, c, tty->fg_color, tty->bg_color);
        } else {
            vid_fb_drawchar(tty->fb, tty->fb_x, tty->fb_y, c, tty->fg_color, tty->bg_color);
        }
        tty->fb_x += char_w;
        if (tty->fb_x + char_w > tty->fb->width) {
            tty->fb_x = 0;
            tty->fb_y += char_h;
        }
    }

    if (tty->fb_y + char_h > tty->fb->height) {
        if (tty->scroll) {
            tty->scroll(tty, 1);
        } else {
            tty->fb_y = 0;
        }
    }

    return 0;
}

static int __tty_puts_impl(tty_t* tty, const char* str) {
    if(!tty || !str) return -1;
    if(!tty->putchar) return -1;

    while (*str) {
        if (tty->putchar(tty, *str) != 0) return -1;
        str++;
    }
    return 0;
}

static int __tty_printf_impl(tty_t* tty, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf((void*)tty->putchar, tty, fmt, args);
    va_end(args);
}

static int __tty_vprintf_impl(tty_t* tty, const char* fmt, va_list args) {
    return vfprintf((void*)tty->putchar, tty, fmt, args);
}

static int __tty_push_char_impl(tty_t* tty, char c) {
    if (!tty->putchar) return -1;
    if (ring_buffer_write(&tty->input_buffer, (uint8_t*)&c, 1) == 0) {
        return -1;
    }
    
    return 0;
}

static int __tty_getchar_impl(tty_t* tty) {
    uint8_t c;
    if (ring_buffer_read(&tty->input_buffer, &c, 1) == 0) {
        return -1;
    }
    return (int)c;
}

static int __tty_clear_impl(tty_t* tty) {
    if (!tty || !tty->fb) {
        LOG_ERROR("TTY clear failed: invalid tty or framebuffer");
        return -1;
    } 
    vid_fb_clear(tty->fb, tty->bg_color);
    tty->fb_x = 0;
    tty->fb_y = 0;
    return 0;
}

static int __tty_scroll_impl(tty_t* tty, int lines) {
    if (!tty || !tty->fb || !tty->fb->buffer) return -1;
    
    uint32_t char_h = 8;
    if(tty->font) {
        char_h = ((psf_font_t*)tty->font)->height;
    }

    uint32_t pixels_to_scroll = lines * char_h;
    if (pixels_to_scroll >= tty->fb->height) {
        tty->clear(tty);
        return 0;
    }
    
    uint32_t rows_to_copy = tty->fb->height - pixels_to_scroll;
    memmove(tty->fb->buffer, tty->fb->buffer + pixels_to_scroll * (tty->fb->pitch / 4), rows_to_copy * tty->fb->pitch);

    for (uint64_t y = rows_to_copy; y < tty->fb->height; y++) {
        for (uint64_t x = 0; x < tty->fb->width; x++) {
            tty->fb->buffer[y * (tty->fb->pitch / 4) + x] = tty->bg_color;
        }
    }

    if (tty->fb_y >= pixels_to_scroll) {
        tty->fb_y -= pixels_to_scroll;
    } else {
        tty->fb_y = 0;
    }

    return 0;
}

static int __tty_cd_impl(tty_t* tty, const char* path) {
    if(!tty || !path) return -1;

    char* abs_path = vfs_resolve_path(tty->pwd, path);
    if (!abs_path) return -1;
    
    vfs_node_t* target = kopen(abs_path);

    if (!target || target->type != VFS_DIRECTORY) {
        khfree(abs_path);
        return -1;
    } else {
        if(tty->pwd) khfree(tty->pwd);
        tty->pwd = abs_path;
        tty->cwd = target;
        return 0;
    }
}

int tty_printf(const char* __fmt, ...) {
    va_list args;
    va_start(args, __fmt);
    vfprintf((void*)active_tty->putchar, active_tty, __fmt, args);
    va_end(args);
}

int tty_init(tty_t* tty, framebuffer_t *fb) {
    if (!tty) return -1;

    tty->flags      = 0;
    
    
    tty->cursor_col = 0;
    tty->cursor_row = 0;

    tty->colors.fg_color_index = ANSI_NUM_WHITE_BOLD;
    tty->colors.bg_color_index = ANSI_NUM_BLACK;

    tty->pwd        = NULL;
    tty->font       = NULL;

    tty->putchar    = __tty_putchar_impl;
    tty->puts       = __tty_puts_impl;
    tty->printf     = __tty_printf_impl;
    tty->vprintf    = __tty_vprintf_impl;
    tty->push_char  = __tty_push_char_impl;
    tty->getchar    = __tty_getchar_impl;
    tty->clear      = __tty_clear_impl;
    tty->scroll     = __tty_scroll_impl;

    tty->cwd        = NULL;
    tty->pwd        = (char*)khmalloc(2);
    if (!tty->pwd) {
        LOG_ERROR("Failed to allocate memory for TTY pwd!");
        return -1;
    }
    strcpy(tty->pwd, "/");
    tty->cd         = __tty_cd_impl;

    tty->fb         = fb;
    tty->fb_x       = 0;
    tty->fb_y       = 0;

    tty->freezed     = false;
    tty->ansi_state  = 0;
    tty->ansi_buf_idx = 0;
    tty->input_buffer = (ring_buffer_t){0};

    if (ring_buffer_init(&tty->input_buffer, 0x1000) != 0) {
        LOG_ERROR("Failed to initialize TTY input buffer!");
        return -1;
    }

    tty->ansi_pallet = default_ansi_pallet;

    tty->fg_color   = tty->ansi_pallet.colors[tty->colors.fg_color_index];
    tty->bg_color   = tty->ansi_pallet.colors[tty->colors.bg_color_index];

    tty->fb->swap = tty->fb->buffer;
    
    if (!tty->fb->buffer) {
        LOG_ERROR("Failed to allocate memory for TTY framebuffer!");
        return -1;
    }

    tty->clear(tty);

    return 0;
}

int tty_load_pallet(tty_t* tty, const pallet_t* pallet) {
    if (!tty)
        return -1;

    if(!pallet) {
        tty->ansi_pallet = default_ansi_pallet;
    } else {
        tty->ansi_pallet = *pallet;
    }
    tty->fg_color = tty->ansi_pallet.colors[tty->colors.fg_color_index];
    tty->bg_color = tty->ansi_pallet.colors[tty->colors.bg_color_index];
    return 0;
}

static psf_font_t* tty_load_font(const char* path) {
    vfs_node_t* font_node = kopen(path);
    if (font_node) {
        uint8_t* font_buf = kmalloc(font_node->size);
        if (font_buf) {
            vfs_read(font_node, 0, font_node->size, font_buf);
            psf_font_t* font = kmalloc(sizeof(psf_font_t));
            if (psf_init_font(font, font_buf) == 0) {
                kclose(font_node);
                return font;
            } else {
                LOG_ERROR("Failed to parse PSF font (in %s)!", path);
                kfree(font);
                kfree(font_buf);
            }
        }
        kclose(font_node);
    } else {
        return NULL;
    }
    return NULL;
}

int tty_load_cfg(tty_t* tty, const char* path) {
    cfg_t* cfg = cfg_parse_file(path);
    if (!cfg) {
        LOG_ERROR("Failed to load TTY config from %s", path);
        return -1;
    }

    const char* font_path = cfg_get_str(cfg, "FONT", NULL);
    if (font_path) {
        psf_font_t* font = tty_load_font(font_path);
        LOG_INFO("Loaded font from %s", font_path);
        if (font) {
            if(tty != NULL) {
                LOG_INFO("Font loaded: %dx%d glyphs", font->width, font->height);
                tty->font = font;
            }
        } else {
            LOG_ERROR("Failed to load font from %s", font_path);
        }
    }

    const char* pal_path = cfg_get_str(cfg, "PAL", NULL);
    if (pal_path) {
        cfg_t* pal_cfg = cfg_parse_file(pal_path);
        if (pal_cfg) {
            pallet_t *pallet = tty == NULL ? &default_ansi_pallet : &tty->ansi_pallet;
            pallet->colors[ANSI_NUM_BLACK] = cfg_get_int(pal_cfg, "BLACK", default_ansi_pallet.colors[ANSI_NUM_BLACK]);
            pallet->colors[ANSI_NUM_BLACK_BOLD] = cfg_get_int(pal_cfg, "BLACK_BOLD", default_ansi_pallet.colors[ANSI_NUM_BLACK_BOLD]);
            pallet->colors[ANSI_NUM_RED] = cfg_get_int(pal_cfg, "RED", default_ansi_pallet.colors[ANSI_NUM_RED]);
            pallet->colors[ANSI_NUM_RED_BOLD] = cfg_get_int(pal_cfg, "RED_BOLD", default_ansi_pallet.colors[ANSI_NUM_RED_BOLD]);
            pallet->colors[ANSI_NUM_GREEN] = cfg_get_int(pal_cfg, "GREEN", default_ansi_pallet.colors[ANSI_NUM_GREEN]);
            pallet->colors[ANSI_NUM_GREEN_BOLD] = cfg_get_int(pal_cfg, "GREEN_BOLD", default_ansi_pallet.colors[ANSI_NUM_GREEN_BOLD]);
            pallet->colors[ANSI_NUM_YELLOW] = cfg_get_int(pal_cfg, "YELLOW", default_ansi_pallet.colors[ANSI_NUM_YELLOW]);
            pallet->colors[ANSI_NUM_YELLOW_BOLD] = cfg_get_int(pal_cfg, "YELLOW_BOLD", default_ansi_pallet.colors[ANSI_NUM_YELLOW_BOLD]);
            pallet->colors[ANSI_NUM_BLUE] = cfg_get_int(pal_cfg, "BLUE", default_ansi_pallet.colors[ANSI_NUM_BLUE]);
            pallet->colors[ANSI_NUM_BLUE_BOLD] = cfg_get_int(pal_cfg, "BLUE_BOLD", default_ansi_pallet.colors[ANSI_NUM_BLUE_BOLD]);
            pallet->colors[ANSI_NUM_MAGENTA] = cfg_get_int(pal_cfg, "MAGENTA", default_ansi_pallet.colors[ANSI_NUM_MAGENTA]);
            pallet->colors[ANSI_NUM_MAGENTA_BOLD] = cfg_get_int(pal_cfg, "MAGENTA_BOLD", default_ansi_pallet.colors[ANSI_NUM_MAGENTA_BOLD]);
            pallet->colors[ANSI_NUM_CYAN] = cfg_get_int(pal_cfg, "CYAN", default_ansi_pallet.colors[ANSI_NUM_CYAN]);
            pallet->colors[ANSI_NUM_CYAN_BOLD] = cfg_get_int(pal_cfg, "CYAN_BOLD", default_ansi_pallet.colors[ANSI_NUM_CYAN_BOLD]);
            pallet->colors[ANSI_NUM_WHITE] = cfg_get_int(pal_cfg, "WHITE", default_ansi_pallet.colors[ANSI_NUM_WHITE]);
            pallet->colors[ANSI_NUM_WHITE_BOLD] = cfg_get_int(pal_cfg, "WHITE_BOLD", default_ansi_pallet.colors[ANSI_NUM_WHITE_BOLD]);
            if(tty != NULL) {  
                tty->fg_color = tty->ansi_pallet.colors[tty->colors.fg_color_index];
                tty->bg_color = tty->ansi_pallet.colors[tty->colors.bg_color_index];
            }
            cfg_destroy(pal_cfg);
        } else {
            LOG_ERROR("Failed to load palette from %s", pal_path);
        }
    } 

    LOG_INFO("TTY config loaded from %s", path);
    tty_cfg = cfg;
    return 0;
}