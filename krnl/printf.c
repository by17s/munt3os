#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include "hw/COM.h"

#include <cstdlib.h>

int lldtoa(char* buffer, uint64_t value, int base) {
    const char* digits = "0123456789abcdef";
    char temp[65]; 
    int i = 0;

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return 1;
    }

    while (value > 0) {
        temp[i++] = digits[value % base];
        value /= base;
    }

    
    for (int j = 0; j < i; j++) {
        buffer[j] = temp[i - j - 1];
    }
    buffer[i] = '\0';

    return i;
}

int itoa(char* buffer, int value, int base) {
    return lldtoa(buffer, (uint64_t)(value < 0 ? -value : value), base);
}

int btoa(char* buffer, bool value) {
    const char* str = value ? "true" : "false";
    int i = 0;
    while (str[i] != '\0') {
        buffer[i] = str[i];
        i++;
    }
    buffer[i] = '\0';
    return i;
}

int __vfprintf_zero_padding(uint64_t v, uint8_t v_width, char* buffer) {
    char vbuff[17] = "0000000000000000";
    int i = v_width;
    for(int i = 0; i < v_width; i++) {
        vbuff[i] = buffer[i];
    }
    for(int i = v_width; i < 16; i++) {
        vbuff[i] = '0';
    }
    vbuff[16] = '\0';
    return 16;
}

int vfprintf(int (*put)(void* put_arg0, char c), void* put_arg0, const char* format, va_list args) {
    if (!format || !put) return -1;

    int written = 0;
    char buffer[32]; 

    for (const char* p = format; *p != '\0'; p++) {
        if (*p != '%') {
            put(put_arg0, *p);
            written++;
            continue;
        }

        p++; 

        int is_left_justify = 0;
        
        while (*p == '-' || *p == ' ') {
            if (*p == '-') is_left_justify = 1;
            p++;
        }

        int is_l = 0;
        int is_ll = 0;
        int is_fill_to_zero = 0;
        int width = 0;
        
        if (*p == '0' && !is_left_justify) {
            is_fill_to_zero = 1;
            p++;
        }

        
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        
        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                is_ll = 1;
                p++;
            } else {
                is_l = 1;
            }
        }

        

        switch (*p) {
            case 'i':
            case 'd': {
                if (is_ll) {
                    long long val = va_arg(args, long long);
                    if (val < 0) { put(put_arg0, '-'); written++; val = -val; }
                    lldtoa(buffer, (uint64_t)val, 10);
                } else if (is_l) {
                    long val = va_arg(args, long);
                    if (val < 0) { put(put_arg0, '-'); written++; val = -val; }
                    lldtoa(buffer, (uint64_t)val, 10);
                } else {
                    int val = va_arg(args, int);
                    if (val < 0) { put(put_arg0, '-'); written++; val = -val; }
                    itoa(buffer, val, 10);
                }
                
                int len = strlen(buffer);
                if (!is_left_justify && width > len) {
                    char pad_char = is_fill_to_zero ? '0' : ' ';
                    for (int i = 0; i < (width - len); i++) { put(put_arg0, pad_char); written++; }
                }
                
                for (int i = 0; buffer[i] != '\0'; i++) { put(put_arg0, buffer[i]); written++; }
                
                if (is_left_justify && width > len) {
                    for (int i = 0; i < (width - len); i++) { put(put_arg0, ' '); written++; }
                }
                break;
            }
            case 'u': {
                if (is_ll) {
                    unsigned long long val = va_arg(args, unsigned long long);
                    lldtoa(buffer, (uint64_t)val, 10);
                } else if (is_l) {
                    unsigned long val = va_arg(args, unsigned long);
                    lldtoa(buffer, (uint64_t)val, 10);
                } else {
                    unsigned int val = va_arg(args, unsigned int);
                    lldtoa(buffer, (uint64_t)val, 10); 
                }
                
                int len = strlen(buffer);
                if (!is_left_justify && width > len) {
                    char pad_char = is_fill_to_zero ? '0' : ' ';
                    for (int i = 0; i < (width - len); i++) { put(put_arg0, pad_char); written++; }
                }
                
                for (int i = 0; buffer[i] != '\0'; i++) { put(put_arg0, buffer[i]); written++; }
                
                if (is_left_justify && width > len) {
                    for (int i = 0; i < (width - len); i++) { put(put_arg0, ' '); written++; }
                }
                break;
            }
            case 'x': {
                if (is_ll) {
                    unsigned long long val = va_arg(args, unsigned long long);
                    lldtoa(buffer, (uint64_t)val, 16);
                } else if (is_l) {
                    unsigned long val = va_arg(args, unsigned long);
                    lldtoa(buffer, (uint64_t)val, 16);
                } else {
                    unsigned int val = va_arg(args, unsigned int);
                    lldtoa(buffer, (uint64_t)val, 16); 
                }
                
                int len = strlen(buffer);
                int target_width = width;
                if (!width && is_fill_to_zero) {
                    target_width = is_ll ? 16 : (is_l ? 16 : 8); 
                }
                
                if (!is_left_justify && target_width > len) {
                    char pad_char = is_fill_to_zero ? '0' : ' ';
                    for (int i = 0; i < (target_width - len); i++) { put(put_arg0, pad_char); written++; }
                }
                
                for (int i = 0; buffer[i] != '\0'; i++) { put(put_arg0, buffer[i]); written++; }
                
                if (is_left_justify && target_width > len) {
                    for (int i = 0; i < (target_width - len); i++) { put(put_arg0, ' '); written++; }
                }
                break;
            }
            case 'p': {
                uint64_t val = (uint64_t)va_arg(args, void*);
                lldtoa(buffer, val, 16);
                
                int len = strlen(buffer);
                int target_width = width > 0 ? width : 16;
                
                if (!is_left_justify && target_width > len) {
                    char pad_char = is_fill_to_zero ? '0' : ' ';
                    for (int i = 0; i < (target_width - len); i++) { put(put_arg0, pad_char); written++; }
                }
                
                for (int i = 0; buffer[i] != '\0'; i++) { put(put_arg0, buffer[i]); written++; }
                
                if (is_left_justify && target_width > len) {
                    for (int i = 0; i < (target_width - len); i++) { put(put_arg0, ' '); written++; }
                }
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) str = "(null)";
                
                int len = strlen(str);
                
                
                
                if (is_left_justify && width > len) {
                    for (int i = 0; i < (width - len); i++) { put(put_arg0, ' '); written++; }
                }
                
                for(int i = 0; str[i] != '\0'; i++) { put(put_arg0, str[i]); written++; }
                
                if (!is_left_justify && width > len) {
                    for (int i = 0; i < (width - len); i++) { put(put_arg0, ' '); written++; }
                }
                break;
            }
            case 'c': {
                char val = (char)va_arg(args, int);
                put(put_arg0, val); written++;
                break;
            }
            case '%': {
                put(put_arg0, '%'); written++;
                break;
            }
            default: {
                
                put(put_arg0, '%'); written++;
                if (is_ll) { put(put_arg0, 'l'); put(put_arg0, 'l'); written += 2; }
                else if (is_l) { put(put_arg0, 'l'); written++; }
                
                if (*p) { put(put_arg0, *p); written++; } 
                else { p--; } 
                break;
            }
        }
    }

    return written;
}