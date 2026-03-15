#include "usb_kbd.h"
#include "../../tty.h"
#include "../../log.h"
#include "../../cstdlib.h"
#include "../../dev/input.h"

LOG_MODULE("usb_kbd")


static const uint8_t usb_hid_to_ascii[128] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`',
    ',', '.', '/', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0
};

static const uint8_t usb_hid_to_ascii_shift[128] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 27, '\b', '\t', ' ', '_', '+', '{', '}', '|', 0, ':', '"', '~',
    '<', '>', '?', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0
};

static uint8_t prev_keys[6] = {0};
static uint8_t caps_lock = 0;
static uint8_t num_lock = 0;

void usb_kbd_handle_report(uint8_t report[8]) {
    
    
    
    
    
    uint8_t modifiers = report[0];
    bool shift_pressed = (modifiers & 0x02) || (modifiers & 0x20);

    
    for (int i = 0; i < 6; i++) {
        uint8_t key = report[2 + i];
        if (key == 0) continue;
        
        bool is_new = true;
        for (int j = 0; j < 6; j++) {
            if (prev_keys[j] == key) { is_new = false; break; }
        }
        if (is_new) {
            if (key == 0x39) caps_lock ^= 1;  
            if (key == 0x53) num_lock ^= 1;   
        }
    }

    
    bool effective_shift = shift_pressed ^ (caps_lock != 0);

    for (int i = 0; i < 6; i++) {
        uint8_t old_key = prev_keys[i];
        if (old_key == 0 || old_key >= 128) continue;
        
        bool still_pressed = false;
        for(int j = 0; j < 6; j++) {
            if (report[2+j] == old_key) {
                still_pressed = true; break;
            }
        }
        if(!still_pressed) {
            input_report_key_kbd(old_key, 0);
        }
    }

    for (int i = 0; i < 6; i++) {
        uint8_t key = report[2 + i];
        
        if (key == 0 || key >= 128) continue;

        
        bool is_new_key = true;
        for (int j = 0; j < 6; j++) {
            if (prev_keys[j] == key) {
                is_new_key = false;
                break;
            }
        }

        if (is_new_key) {
            input_report_key_kbd(key, 1);
            
            
            
            bool use_shift;
            if (key >= 0x04 && key <= 0x1D) {
                use_shift = effective_shift;
            } else {
                use_shift = shift_pressed;
            }
            uint8_t ascii = use_shift ? usb_hid_to_ascii_shift[key] : usb_hid_to_ascii[key];
            
            if (ascii) {
                tty_t* active_tty;
                if (tty_get(-1, &active_tty) == 0 && active_tty) {
                    active_tty->push_char(active_tty, ascii);
                }
            }
        }
    }

    input_sync_kbd();

    
    for (int i = 0; i < 6; i++) {
        prev_keys[i] = report[2 + i];
    }
}
