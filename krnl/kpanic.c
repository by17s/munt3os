#include "kpanic.h"

#include "printf.h"

int kpanic(struct interrupt_frame *frame, const char *format, ...) {
    tty_t *tty;
    tty_get(-1, &tty);
    tty->clear(tty);

    tty_printf(":( *** Kernel Panic *** \n");
    va_list args;
    va_start(args, format);
    tty->vprintf(tty, format, args);
    tty_printf("\n");
    va_end(args);

    uint64_t cr2, cr3;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    tty_printf("-- CPU State --\n");
    tty_printf("RIP          = 0x%016llx   CS           = 0x%016llx\n", frame->ip, frame->cs);
    tty_printf("RFLAGS       = 0x%016llx   RSP          = 0x%016llx\n", frame->flags, frame->sp);
    tty_printf("SS           = 0x%016llx   CR2          = 0x%016llx\n", frame->ss, cr2);
    tty_printf("CR3          = 0x%016llx\n", cr3);
    tty_printf("-- General Purpose Registers --\n");
    tty_printf("Nothing yet... :( \n");
    tty_printf("-- Bug report information --\n");
    tty_printf("Please report this issue at https://github.com/by17s/munt3os\n");
    return 0;
}
