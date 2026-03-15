#ifndef KERNEL_H
#define KERNEL_H

#include <tty.h>
#include <stddef.h>

int kmain(tty_t *tty, int flags, void* initrd_base, size_t initrd_size);

#endif 