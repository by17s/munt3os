#include "cstdlib.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <printf.h>

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memcpy_sse(void* dest, const void* src, size_t n) {
    
    
    return memcpy_fast(dest, src, n);
}

void* memset(void* dest, int val, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)val;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d < s) {
        
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}


void* memcpy_fast(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (n < 8) {
        while (n--) *d++ = *s++;
        return dest;
    }

    while (((uintptr_t)d & 7) != 0 && n > 0) {
        *d++ = *s++;
        n--;
    }

    uint64_t* d64 = (uint64_t*)d;
    const uint64_t* s64 = (const uint64_t*)s;

    size_t blocks = n / 32;
    while (blocks--) {
        d64[0] = s64[0];
        d64[1] = s64[1];
        d64[2] = s64[2];
        d64[3] = s64[3];
        d64 += 4;
        s64 += 4;
    }

    n %= 32;
    size_t words = n / 8;
    while (words--) {
        *d64++ = *s64++;
    }

    
    d = (uint8_t*)d64;
    s = (const uint8_t*)s64;
    n %= 8;
    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

char* strtok(char* str, const char* delim) {
    static char* next_token = NULL;
    if (str != NULL) {
        next_token = str;
    }

    if (next_token == NULL) {
        return NULL;
    }

    
    while (*next_token && strchr(delim, *next_token)) {
        next_token++;
    }

    if (*next_token == '\0') {
        next_token = NULL;
        return NULL;
    }

    char* token = next_token;

    
    while (*next_token && !strchr(delim, *next_token)) {
        next_token++;
    }

    if (*next_token != '\0') {
        *next_token = '\0';
        next_token++;
    } else {
        next_token = NULL;
    }

    return token;
}

char* strchr(const char* str, int c) {
    while (*str) {
        if (*str == (char)c) {
            return (char*)str;
        }
        str++;
    }
    if (c == '\0') {
        return (char*)str;
    }
    return NULL;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return (unsigned char)(*s1) - (unsigned char)(*s2);
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0' || s2[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

int strlen(const char* str) {
    int len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcpy(char* dest, const char* src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return i;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for ( ; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    size_t dest_len = strlen(dest);
    size_t i;
    for (i = 0; src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i;

    for (i = 0 ; i < n && src[i] != '\0' ; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';
    return dest;
}

void reverse_str(char *str) {
	int len = 0;
	while (str[len] != '\0') { len++; }

	int start = 0;
	int end = len - 1;

	while (start < end) {
		char temp = str[start];
		str[start] = str[end];
		str[end] = temp;
		start++;
		end--;
	}
}


void __tool_int_to_str(int64_t i, uint8_t base, char *buf) {
	bool negative = false;

	
	if (i < 0) {
		negative = true;
		i *= -1; 
	}

	int64_t index = 0;
	
	do {
		int64_t remainder = i % base;
		
		buf[index++] =
		    (remainder > 9) ? (remainder - 10) + 'A' : remainder + '0'; 
		i /= base;
	} while (i > 0);

	
	if (negative) { buf[index++] = '-'; }

	
	buf[index] = '\0';

	
	reverse_str(buf);
}



void __tool_uint_to_str(uint64_t i, uint8_t base, char *buf) {
	uint64_t index = 0;
	
	do {
		uint64_t remainder = i % base;
		
		buf[index++] =
		    (remainder > 9) ? (remainder - 10) + 'A' : remainder + '0'; 
		i /= base;
	} while (i > 0);

	
	buf[index] = '\0';

	
	reverse_str(buf);
}

typedef struct {
    char* buffer;
    size_t size;
    size_t pos;
} _snprintf_ctx_t;

static int _snprintf_putc(void* ctx, char c) {
    _snprintf_ctx_t* sctx = (_snprintf_ctx_t*)ctx;
    if (sctx->pos < sctx->size - 1) {
        sctx->buffer[sctx->pos++] = c;
        sctx->buffer[sctx->pos] = '\0';
    }
    return 0;
}

int snprintf(char* buffer, size_t size, const char* fmt, ...) {
    _snprintf_ctx_t ctx = { .buffer = buffer, .size = size, .pos = 0 };
    va_list args;
    va_start(args, fmt);
    vfprintf(_snprintf_putc, &ctx, fmt, args);
    va_end(args);
    return ctx.pos;
}