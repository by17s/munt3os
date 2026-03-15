#pragma once

#include "hw/COM.h"
#include "printf.h"

#define LOG_MODULE(name) \
    static const char* __log_module_name = name;

#define LOG(lvl_str, fmt, ...) \
    do { \
        log_print("[%s] [%s:%d] " fmt "\n", lvl_str, __log_module_name, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define LOG_INFO(fmt, ...)  LOG("INFO ", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG("WARN ", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG("ERROR", fmt, ##__VA_ARGS__)

#define LOG_DEV_COM1 0

extern void (*log_print)(const char* fmt, ...);
extern int tty_printf(const char* fmt, ...);

void __log_printf_com1(const char* fmt, ...);

static inline void log_init(int dev) {
    if (dev == LOG_DEV_COM1) {
        com_init(COM1_PORT);
        log_print = __log_printf_com1;
    } else {
        log_print = (void (*)(const char*, ...))tty_printf;
    }
}