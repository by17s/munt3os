#include "ps2.h"

#include <memio.h>
#include <log.h>
#include <hw/lapic.h>
#include <tty.h>
#include "idt.h"
#include <dev/input.h>

LOG_MODULE("ps2")

#define PS2_DATA_PORT 0x60
#define PS2_CMD_PORT  0x64

#define RSHIFT_P 0x36
#define RSHIFT_R 0xB6

static uint8_t KEYBOARD_SCANCODES[128] =
    {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8',    
        '9', '0', '-', '=', '\b',                         
        '\t',                                             
        'q', 'w', 'e', 'r',                               
        't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',     
        0,                                                
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 
        '\'', '`', 0,                                     
        '\\', 'z', 'x', 'c', 'v', 'b', 'n',               
        'm', ',', '.', '/', 0,                            
        '*',
        0,   
        ' ', 
        0,   
        0,   
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 
        0, 
        0, 
        0, 
        0, 
        0, 
        '-',
        0, 
        0,
        0, 
        '+',
        0, 
        0, 
        0, 
        0, 
        0, 
        0, 0, 0,
        0, 
        0, 
        0, 
};

static uint8_t KEYBOARD_SCANCODES_UPPER[128] =
    {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*',    
        '(', ')', '_', '+', '\b',                         
        '\t',                                             
        'Q', 'W', 'E', 'R',                               
        'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',     
        0,                                                
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', 
        '\"', '~', 0,                                     
        '|', 'Z', 'X', 'C', 'V', 'B', 'N',                
        'M', '<', '>', '/', 0,                            
        '*',
        0,   
        ' ', 
        0,   
        0,   
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 
        0, 
        0, 
        0, 
        0, 
        0, 
        '-',
        0, 
        0,
        0, 
        '+',
        0, 
        0, 
        0, 
        0, 
        0, 
        0, 0, 0,
        0, 
        0, 
        0, 
};


static void ps2_wait_write(void) {
    int timeout = 100000;
    while ((inb(PS2_CMD_PORT) & 2) && timeout--); 
}

static void ps2_wait_read(void) {
    int timeout = 100000;
    while (!(inb(PS2_CMD_PORT) & 1) && timeout--); 
}

static void ps2_write_mouse(uint8_t data) {
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xD4); 
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
    ps2_wait_read();
    inb(PS2_DATA_PORT); 
}





static bool shift_pressed = false;

__attribute__((interrupt))
void isr_ps2_keyboard(struct interrupt_frame* frame) {
    uint8_t scancode = inb(PS2_DATA_PORT);

    
    bool pressed = !(scancode & 0x80);
    uint8_t key = scancode & 0x7F;

    switch (key)
    {
    case 0x2A: 
        shift_pressed = pressed;
        break;
    case 0x36: 
        shift_pressed = pressed;
        break;
    default:
        break;
    }

    if (pressed && shift_pressed) {
        key = KEYBOARD_SCANCODES_UPPER[key];
    } else if (pressed && !shift_pressed) {
        key = KEYBOARD_SCANCODES[key];
    }

    
    uint8_t linux_code = scancode & 0x7F; 
    input_report_key_kbd(linux_code, pressed ? 1 : 0);
    input_sync_kbd();

    if (pressed) {
        
        tty_t* active_tty;
        if (tty_get(-1, &active_tty) == 0 && active_tty) {
            if (key != 0) {
                active_tty->push_char(active_tty, key);
            }
        }
    } else {
        
    }

    
    outb(0x20, 0x20);
}


static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[3];

__attribute__((interrupt))
void isr_ps2_mouse(struct interrupt_frame* frame) {
    uint8_t data = inb(PS2_DATA_PORT);

    
    
    
    if (mouse_cycle == 0 && (!(data & 0x08) || data == 0xFA || data == 0xFE)) {
        outb(0xA0, 0x20); 
        outb(0x20, 0x20); 
        return;
    }

    mouse_bytes[mouse_cycle++] = data;
    
    if (mouse_cycle == 3) { 
        
        mouse_cycle = 0;
        
        
        if (mouse_bytes[0] & 0x08) {
            bool left_click = mouse_bytes[0] & 0x01;
            bool right_click = mouse_bytes[0] & 0x02;
            int32_t x_mov = (int32_t)mouse_bytes[1] - ((mouse_bytes[0] & 0x10) ? 256 : 0);
            int32_t y_mov = (int32_t)mouse_bytes[2] - ((mouse_bytes[0] & 0x20) ? 256 : 0);

            
            if (x_mov != 0 || y_mov != 0 || left_click || right_click) {
                
                input_report_rel_mouse(REL_X, x_mov);
                input_report_rel_mouse(REL_Y, -y_mov); 
                input_report_key_mouse(BTN_LEFT, left_click);
                input_report_key_mouse(BTN_RIGHT, right_click);
                input_sync_mouse();
            }
        }
    }

    outb(0xA0, 0x20); 
    outb(0x20, 0x20); 
}

void ps2_init(void) {
    LOG_INFO("PS/2: Initializing controller...");

    
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xAD); 
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xA7); 

    
    inb(PS2_DATA_PORT);

    
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0x20);
    ps2_wait_read();
    uint8_t config = inb(PS2_DATA_PORT);

    
    config |= (1 << 0);
    config |= (1 << 1);
    config &= ~(1 << 4); 
    config &= ~(1 << 5);

    
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0x60);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xAE);
    ps2_wait_write();
    outb(PS2_CMD_PORT, 0xA8);

    
    ps2_write_mouse(0xF6); 
    ps2_write_mouse(0xF4); 

    LOG_INFO("PS/2: Keyboard and Mouse initialized and ready");
}