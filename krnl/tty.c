#include "tty.h"

#include "cstdlib.h"
#include "mem/kheap.h"
#include "font/psf.h"
#include "mm.h"

#include "log.h"
LOG_MODULE("tty");

static tty_t tty_array[4];
static tty_t* active_tty = NULL;

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

    tty->ansi_pallet = (pallet_t){
        0x171717, 
        0x2B2B2B, 

        0xD25252, 
        0xF00C0C, 

        0xA5C261, 
        0xC2E075, 

        0xFFC66D, 
        0xE1E48B, 

        0x6C99BB, 
        0x8AB7D9, 

        0xD191D9, 
        0xEFB5F7, 

        0xBED6FF, 
        0xDCF4FF, 

        0xEEEEEC, 
        0xFFFFFF  
    };

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