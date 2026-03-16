#pragma once

#include <stdint.h>
#include <stdarg.h>

#include "tty.h"
#include "hw/idt.h"

int kpanic(struct interrupt_frame* frame, const char* format, ...);