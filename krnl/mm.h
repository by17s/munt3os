#pragma once

#include <stddef.h>

#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kheap.h"
#include "mem/kslab.h"

int mm_init(void);

void* kmalloc(size_t size);
void* kcalloc(size_t count, size_t size);
void* krealloc(void* ptr, size_t new_size);

void kfree(void* ptr);
void kcfree(void* ptr, size_t count, size_t size);