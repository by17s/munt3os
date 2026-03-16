#pragma once

#include <stdint.h>
#include <stddef.h>
#include "cstdlib.h"

typedef struct {
    uint8_t bytes[32];
} sha256_digest_t;

void sha256(const void* data, size_t len, sha256_digest_t* out);
void sha256_to_hex(const sha256_digest_t* digest, char* buf);

static inline void sha256_oneshot(const void* data, size_t len, char *out_hex) {
    sha256_digest_t digest;
    sha256(data, len, &digest);
    sha256_to_hex(&digest, out_hex);
    return;
}

static inline int sha256_compare(const sha256_digest_t* a, const sha256_digest_t* b) {
    return memcmp(a->bytes, b->bytes, 32);
}