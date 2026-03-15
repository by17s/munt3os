#define COM1_PORT 0x3F8

#include "memio.h"

void com_init(int com_port) {
    outb(com_port + 1, 0x00);    
    outb(com_port + 3, 0x80);    
    outb(com_port + 0, 0x03);    
    outb(com_port + 1, 0x00);    
    outb(com_port + 3, 0x03);    
    outb(com_port + 2, 0xC7);    
    outb(com_port + 4, 0x0B);    
}

static inline int com_is_transmit_empty(int com_port) {
    return inb(com_port + 5) & 0x20;
}

void com_write_char(int com_port, char c) {
    while (com_is_transmit_empty(com_port) == 0) {
        asm volatile ("pause");
    }
    outb(com_port, c);
}

void com_print(int com_port, const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        com_write_char(com_port, str[i]);
    }
}