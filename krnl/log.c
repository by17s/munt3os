#include <stdarg.h>
#include <stdint.h>

#include <cstdlib.h>

#include "hw/COM.h"
#include "printf.h"

int __log_put_char_com1(void* arg, char c) {
    com_write_char(COM1_PORT, c);
    return 0;
}

void __log_printf_com1(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(__log_put_char_com1, NULL, fmt, args);
    va_end(args);
}

void (*log_print)(const char* fmt, ...) = __log_printf_com1;