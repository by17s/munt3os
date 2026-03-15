#pragma once

#include <stdint.h>

#ifndef NULL
# define NULL ((void*)0)
#endif

typedef unsigned long size_t;

void* memcpy(void* dest, const void* src, size_t n);
void* memcpy_fast(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
void* memset(void* dest, int val, size_t n);
void* memmove(void* dest, const void* src, size_t n);

int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
int strlen(const char* str);
int strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);

char* strtok(char* str, const char* delim);
char* strchr(const char* str, int c);

void reverse_str(char *str);
void __tool_int_to_str(int64_t i, uint8_t base, char *buf);
void __tool_uint_to_str(uint64_t i, uint8_t base, char *buf);

static inline int isspace(char c) {
    return c == ' ' || c == '\t';
}

static inline int isdigit(char c) {
    return c >= '0' && c <= '9';
}

int snprintf(char* buffer, size_t size, const char* fmt, ...);