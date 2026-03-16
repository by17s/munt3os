#include "idt.h"

#include <memio.h>
#include "cstdlib.h"
#include "log.h"

#include "kpanic.h"

LOG_MODULE("idt");


typedef struct {
    uint16_t isr_low;      
    uint16_t kernel_cs;    
    uint8_t  ist;          
    uint8_t  attributes;   
    uint16_t isr_mid;      
    uint32_t isr_high;     
    uint32_t reserved;     
} __attribute__((packed)) idt_entry_t;


typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;


__attribute__((aligned(0x10))) 
static idt_entry_t idt[256];
static idtr_t idtr;

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags) {
    uint64_t addr = (uint64_t)isr;
    
    idt[vector].isr_low    = (uint16_t)(addr & 0xFFFF);
    idt[vector].kernel_cs  = 0x28; 
    idt[vector].ist        = 0;
    idt[vector].attributes = flags;
    idt[vector].isr_mid    = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].isr_high   = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved   = 0;
}

#define HANG() asm volatile("cli; hlt")

__attribute__((interrupt)) 
static void isr_divide_by_zero(struct interrupt_frame* frame) {
    LOG_ERROR("[PANIC] Divide by Zero! RIP: 0x%p, CS: 0x%llx, RFLAGS: 0x%llx, RSP: 0x%p, SS: 0x%llx\n", 
            frame->ip, frame->cs, frame->flags, frame->sp, frame->ss);
    kpanic(frame, "#DE: Divide by Zero");
    HANG();
}

__attribute__((interrupt)) 
static void isr_gpf(struct interrupt_frame* frame, uint64_t error_code) {
    LOG_ERROR("[PANIC] General Protection Fault (GPF)! Error Code: 0x%llx, RIP: 0x%p, CS: 0x%llx, RFLAGS: 0x%llx, RSP: 0x%p, SS: 0x%llx\n", 
            error_code, frame->ip, frame->cs, frame->flags, frame->sp, frame->ss);
    kpanic(frame, "#GPF: General Protection Fault");
    HANG();
}

__attribute__((interrupt)) 
static void isr_page_fault(struct interrupt_frame* frame, uint64_t error_code) {
    uint64_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));
    
    LOG_ERROR("[PANIC] Page Fault! Address: 0x%p, RIP: 0x%p, Error Code: 0x%llx\n", faulting_address, frame->ip, error_code);
    kpanic(frame, "#PF: Page Fault at address 0x%llx (cr2)", faulting_address);
    HANG();
}


__attribute__((interrupt)) 
static void isr_breakpoint(struct interrupt_frame* frame) {
    LOG_INFO("Breakpoint at 0x%llx", frame->ip);
    kpanic(frame, "Kernel panic triggered by breakpoint at RIP: 0x%llx", frame->ip);
    asm volatile("cli; hlt"); 
}

void pic_remap(void) {
    uint8_t m1 = inb(0x21); 
    uint8_t m2 = inb(0xA1);

    
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    
    outb(0x21, 0x20); 
    outb(0xA1, 0x28); 

    
    outb(0x21, 4);
    outb(0xA1, 2);

    
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    
    
    m1 &= ~(1 << 1); 
    m1 &= ~(1 << 2); 
    m2 &= ~(1 << 4); 

    outb(0x21, m1);
    outb(0xA1, m2);
    
    LOG_INFO("PIC: Legacy PIC remapped to vectors 0x20-0x2F");
}

extern void isr_ps2_keyboard(struct interrupt_frame* frame);
extern void isr_ps2_mouse(struct interrupt_frame* frame);

void idt_init(void) {
    
    idtr.base = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * 256 - 1;

    for (int i = 0; i < 256; i++) {
        memset(&idt[i], 0, sizeof(idt_entry_t)); 
    }

    
    idt_set_descriptor(0x00, isr_divide_by_zero, 0x8E);
    idt_set_descriptor(0x03, isr_breakpoint,     0x8E);
    idt_set_descriptor(0x0D, isr_gpf,            0x8E);
    idt_set_descriptor(0x0E, isr_page_fault,     0x8E);

    
    idt_set_descriptor(0x21, isr_ps2_keyboard, 0x8E); 
    idt_set_descriptor(0x2C, isr_ps2_mouse,    0x8E); 

    
    asm volatile ("lidt %0" : : "m"(idtr));
    
    LOG_INFO("IDT initialized successfully");
}