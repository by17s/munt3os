#pragma once
#include <stdint.h>

struct interrupt_frame {
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
};

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags);
void idt_init(void);


void pic_remap(void);

#define STI() asm volatile("sti")
#define CLI() asm volatile("cli")